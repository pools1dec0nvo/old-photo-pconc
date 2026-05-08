/************************************************************************
 * old-photo-parallel-B
 * Carolina João (109587) e Tiago Lopes Carvalho (106396)
 * Programção Concorrente - LEEC 2024/25
 * 
 * Especiais agradecimentos ao professor JNOS.
 *
 * "If you have any trouble sounding condescending, find a Unix user to show you how it's done" - Scott Adams
 * Nota: Nenhuma máquina Windows prejudicou o nosso desenvolvimento :D
 ************************************************************************/

#include <inttypes.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include "image-lib.h"

struct ImageInfo {
    char *filename;
    char *fullPathName;
    off_t size;
};

static struct ImageInfo *images = NULL;
static int nnFiles = 0;
static gdImagePtr inTextureImage = NULL;
static int threadCount = 1;

static struct timespec startTimeTotal, endTimeTotal;
static struct timespec startTimeSeq, endTimeSeq;
static struct timespec startTimePar, endTimePar;

struct timespec *threadStartTimes = NULL;
struct timespec *threadEndTimes = NULL;

static char outputDir[1024];

static volatile int processedCount = 0;
static pthread_mutex_t processedCountMutex = PTHREAD_MUTEX_INITIALIZER;

static int pipefd[2];
static volatile int done = 0;
static pthread_t *threads = NULL;
static int *threadIds = NULL;
static pthread_t keyboardThread;

/**
 * string_tolower
 * Argumentos: str (char*)
 * Retorna: void
 * Objetivo: converter a string para minúsculas
 */
void string_tolower(char *str) {
    while (*str) {
        *str = (char)tolower((unsigned char)*str);
        str++;
    }
}

/**
 * cmp_name
 * Argumentos: a (const void*), b (const void*)
 * Retorna: int
 * Objetivo: comparar nomes de ficheiros para ordenação
 */
int cmp_name(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    return strcmp(ia->filename, ib->filename);
}

/**
 * cmp_size
 * Argumentos: a (const void*), b (const void*)
 * Retorna: int
 * Objetivo: comparar tamanhos de ficheiros para ordenação
 */
int cmp_size(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    if (ia->size < ib->size) return -1;
    else if (ia->size > ib->size) return 1;
    return 0;
}

/**
 * process_image
 * Argumentos: filename (const char*), fullPathName (const char*), 
 *             inTextureImage (gdImagePtr), outputDir (const char*)
 * Retorna: void
 * Objetivo: processar a imagem de entrada e guardar no diretório de saída
 */
void process_image(const char *filename, const char *fullPathName,
                   gdImagePtr inTextureImage, const char *outputDir) {
    char outFileName[1024];
    snprintf(outFileName, sizeof(outFileName), "%s%s", outputDir, filename);

    if (access(outFileName, F_OK) != -1) {
        return;
    }

    gdImagePtr inImage = read_jpeg_file((char *)fullPathName);
    if (inImage == NULL) {
        return;
    }

    gdImagePtr outContrastImage = contrast_image(inImage);
    gdImagePtr outSmoothedImage = smooth_image(outContrastImage);
    gdImagePtr outTexturedImage = texture_image(outSmoothedImage, inTextureImage);
    gdImagePtr outSepiaImage = sepia_image(outTexturedImage);

    write_jpeg_file(outSepiaImage, outFileName);

    gdImageDestroy(outContrastImage);
    gdImageDestroy(outSmoothedImage);
    gdImageDestroy(outTexturedImage);
    gdImageDestroy(outSepiaImage);
    gdImageDestroy(inImage);
}

/**
 * thread_function
 * Argumentos: arg (void*)
 * Retorna: void*
 * Objetivo: função executada por cada thread para processar as imagens
 */
void *thread_function(void *arg) {
    int threadId = *((int *)arg);
    clock_gettime(CLOCK_MONOTONIC, &threadStartTimes[threadId]);

    while (1) {
        int idx;
        ssize_t n = read(pipefd[0], &idx, sizeof(idx));
        if (n != sizeof(idx)) {
            break;
        }
        process_image(images[idx].filename, images[idx].fullPathName,
                      inTextureImage, outputDir);
        pthread_mutex_lock(&processedCountMutex);
        processedCount++;
        pthread_mutex_unlock(&processedCountMutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &threadEndTimes[threadId]);
    return NULL;
}

/**
 * keyboard_function
 * Argumentos: arg (void*)
 * Retorna: void*
 * Objetivo: capturar entradas do teclado e apresentar tempo médio de processamento/ nr de imgs processadas
 */
void *keyboard_function(void *arg) {
    (void)arg;

    while (!done) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        } else if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            int c = getchar();
            if (c == EOF) {
                break;
            }
            if (c == 'S' || c == 's') {
                pthread_mutex_lock(&processedCountMutex);
                int currentCount = processedCount;
                pthread_mutex_unlock(&processedCountMutex);

                if (currentCount == 0) {
                    fprintf(stderr,
                            "[INFO] Já foram processadas %d / %d imagens. (Ainda nenhuma imagem processada)\n",
                            currentCount, nnFiles);
                } else {
                    struct timespec currentTime;
                    clock_gettime(CLOCK_MONOTONIC, &currentTime);
                    struct timespec elapsedParallel = diff_timespec(&currentTime, &startTimePar);
                    double elapsedSeconds = elapsedParallel.tv_sec + (elapsedParallel.tv_nsec / 1e9);
                    double averageTimePerImage = elapsedSeconds / currentCount;

                    fprintf(stderr,
                            "[INFO] Já foram processadas %d / %d imagens.\n"
                            "       Tempo médio por imagem: %.6f s\n",
                            currentCount, nnFiles, averageTimePerImage);
                }
            }
        }
    }

    return NULL;
}

/**
 * main
 * Argumentos: argc (int), argv (char**)
 * Retorna: int
 * Objetivo: ponto de entrada do programa, gere argumentos e cria threads de processamento
 */
int main(int argc, char* argv[]) {
    char *dirPath;
    struct dirent *dirEntry;
    struct ImageInfo *temp;
    size_t capacity = 0;

    struct timespec parTime, seqTime, totalTime;

    clock_gettime(CLOCK_MONOTONIC, &startTimeTotal);
    clock_gettime(CLOCK_MONOTONIC, &startTimeSeq);

    if (argc < 4) {
        fprintf(stderr, "Uso: <caminho-directoria> <n_threads> <-name|-size>\n");
        exit(1);
    }

    dirPath = argv[1];
    if (sscanf(argv[2], "%d", &threadCount) != 1) {
        threadCount = 1;
    }
    if (threadCount <= 0) {
        threadCount = 1;
    }

    char sortKind[16];
    strncpy(sortKind, argv[3], sizeof(sortKind) - 1);
    sortKind[sizeof(sortKind) - 1] = '\0';
    string_tolower(sortKind);

    DIR *d = opendir(dirPath);
    if (!d) {
        fprintf(stderr, "Não foi possível abrir a directoria: %s\n", dirPath);
        exit(1);
    }

    nnFiles = 0;
    temp = NULL;

    while ((dirEntry = readdir(d)) != NULL) {
        if (dirEntry->d_type == DT_REG) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, dirEntry->d_name);

            struct stat st;
            if (stat(fullPath, &st) == 0) {
                const char *extension = strrchr(dirEntry->d_name, '.');
                if (extension != NULL &&
                    (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0)) {

                    if (nnFiles == (int)capacity) {
                        if (capacity == 0) {
                            capacity = 16;
                        } else {
                            capacity *= 2;
                        }
                        temp = realloc(images, capacity * sizeof(struct ImageInfo));
                        if (!temp) {
                            fprintf(stderr, "Falha na alocação de memória\n");
                            closedir(d);
                            free(images);
                            exit(1);
                        }
                        images = temp;
                    }
                    images[nnFiles].filename = strdup(dirEntry->d_name);
                    images[nnFiles].fullPathName = strdup(fullPath);
                    images[nnFiles].size = st.st_size;
                    nnFiles++;
                }
            }
        }
    }
    closedir(d);

    if (nnFiles == 0) {
        free(images);
        exit(0);
    }

    if (strcmp(sortKind, "-name") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    } else if (strcmp(sortKind, "-size") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_size);
    } else {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    }

    inTextureImage = read_png_file("./paper-texture.png");

    snprintf(outputDir, sizeof(outputDir), "%s/old_photo_PAR_B", dirPath);
    {
        size_t len = strlen(outputDir);
        if (len + 1 < sizeof(outputDir)) {
            if (len > 0 && outputDir[len - 1] != '/') {
                strncat(outputDir, "/", sizeof(outputDir) - len - 1);
            }
        } else {
            fprintf(stderr, "Caminho do diretório de saída muito longo.\n");
            exit(1);
        }
    }

    if (!create_directory(outputDir)) {
        fprintf(stderr, "Não foi possível criar a directoria de saída %s\n", outputDir);
    }

    clock_gettime(CLOCK_MONOTONIC, &endTimeSeq);
    clock_gettime(CLOCK_MONOTONIC, &startTimePar);

    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(1);
    }

    threads = malloc(sizeof(pthread_t) * threadCount);
    threadIds = malloc(sizeof(int) * threadCount);
    threadStartTimes = malloc(sizeof(struct timespec) * threadCount);
    threadEndTimes = malloc(sizeof(struct timespec) * threadCount);

    if (!threads || !threadIds || !threadStartTimes || !threadEndTimes) {
        fprintf(stderr, "Falha na alocação de memória para threads\n");
        free(threads);
        free(threadIds);
        free(threadStartTimes);
        free(threadEndTimes);
        for (int i = 0; i < nnFiles; i++) {
            free(images[i].filename);
            free(images[i].fullPathName);
        }
        free(images);
        if (inTextureImage) gdImageDestroy(inTextureImage);
        exit(1);
    }

    for (int i = 0; i < threadCount; i++) {
        threadIds[i] = i;
        pthread_create(&threads[i], NULL, thread_function, &threadIds[i]);
    }

    pthread_create(&keyboardThread, NULL, keyboard_function, NULL);

    for (int i = 0; i < nnFiles; i++) {
        int idx = i;
        ssize_t n = write(pipefd[1], &idx, sizeof(idx));
        if (n != sizeof(idx)) {
            break;
        }
    }

    close(pipefd[1]);

    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }

    done = 1;
    pthread_join(keyboardThread, NULL);
    close(pipefd[0]);

    if (inTextureImage) gdImageDestroy(inTextureImage);

    for (int i = 0; i < nnFiles; i++) {
        free(images[i].filename);
        free(images[i].fullPathName);
    }
    free(images);
    free(threads);
    free(threadIds);

    clock_gettime(CLOCK_MONOTONIC, &endTimePar);
    clock_gettime(CLOCK_MONOTONIC, &endTimeTotal);

    parTime = diff_timespec(&endTimePar, &startTimePar);
    seqTime = diff_timespec(&endTimeSeq, &startTimeSeq);
    totalTime = diff_timespec(&endTimeTotal, &startTimeTotal);

    {
        char timingFileName[2048];
        if (strcmp(sortKind, "-name") == 0) {
            snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-name.txt", outputDir, threadCount);
        } else {
            snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-size.txt", outputDir, threadCount);
        }

        FILE *fout = fopen(timingFileName, "w");
        if (fout) {
            fprintf(fout, "Tempo sequencial: %jd.%09ld s\n", (intmax_t)seqTime.tv_sec, seqTime.tv_nsec);
            fprintf(fout, "Tempo paralelo: %jd.%09ld s\n", (intmax_t)parTime.tv_sec, parTime.tv_nsec);
            fprintf(fout, "Tempo total: %jd.%09ld s\n", (intmax_t)totalTime.tv_sec, totalTime.tv_nsec);

            for (int i = 0; i < threadCount; i++) {
                struct timespec threadDiff = diff_timespec(&threadEndTimes[i], &threadStartTimes[i]);
                fprintf(fout, "Thread %d tempo: %jd.%09ld s\n", i, (intmax_t)threadDiff.tv_sec, threadDiff.tv_nsec);
            }
            fclose(fout);
        }
    }

    free(threadStartTimes);
    free(threadEndTimes);

    return 0;
}/************************************************************************
 * old-photo-parallel-B
 * Carolina João (109587) e Tiago Lopes Carvalho (106396)
 * Programção Concorrente - LEEC 2024/25
 * 
 * Especiais agradecimentos ao professor JNOS.
 *
 * "If you have any trouble sounding condescending, find a Unix user to show you how it's done" - Scott Adams
 * Nota: Nenhuma máquina Windows prejudicou o nosso desenvolvimento :D
 ************************************************************************/

#include <inttypes.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include "image-lib.h"

struct ImageInfo {
    char *filename;
    char *fullPathName;
    off_t size;
};

static struct ImageInfo *images = NULL;
static int nnFiles = 0;
static gdImagePtr inTextureImage = NULL;
static int threadCount = 1;

static struct timespec startTimeTotal, endTimeTotal;
static struct timespec startTimeSeq, endTimeSeq;
static struct timespec startTimePar, endTimePar;

struct timespec *threadStartTimes = NULL;
struct timespec *threadEndTimes = NULL;

static char outputDir[1024];

static volatile int processedCount = 0;
static pthread_mutex_t processedCountMutex = PTHREAD_MUTEX_INITIALIZER;

static int pipefd[2];
static volatile int done = 0;
static pthread_t *threads = NULL;
static int *threadIds = NULL;
static pthread_t keyboardThread;

/**
 * string_tolower
 * Argumentos: str (char*)
 * Retorna: void
 * Objetivo: converter a string para minúsculas
 */
void string_tolower(char *str) {
    while (*str) {
        *str = (char)tolower((unsigned char)*str);
        str++;
    }
}

/**
 * cmp_name
 * Argumentos: a (const void*), b (const void*)
 * Retorna: int
 * Objetivo: comparar nomes de ficheiros para ordenação
 */
int cmp_name(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    return strcmp(ia->filename, ib->filename);
}

/**
 * cmp_size
 * Argumentos: a (const void*), b (const void*)
 * Retorna: int
 * Objetivo: comparar tamanhos de ficheiros para ordenação
 */
int cmp_size(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    if (ia->size < ib->size) return -1;
    else if (ia->size > ib->size) return 1;
    return 0;
}

/**
 * process_image
 * Argumentos: filename (const char*), fullPathName (const char*), 
 *             inTextureImage (gdImagePtr), outputDir (const char*)
 * Retorna: void
 * Objetivo: processar a imagem de entrada e guardar no diretório de saída
 */
void process_image(const char *filename, const char *fullPathName,
                   gdImagePtr inTextureImage, const char *outputDir) {
    char outFileName[1024];
    snprintf(outFileName, sizeof(outFileName), "%s%s", outputDir, filename);

    if (access(outFileName, F_OK) != -1) {
        return;
    }

    gdImagePtr inImage = read_jpeg_file((char *)fullPathName);
    if (inImage == NULL) {
        return;
    }

    gdImagePtr outContrastImage = contrast_image(inImage);
    gdImagePtr outSmoothedImage = smooth_image(outContrastImage);
    gdImagePtr outTexturedImage = texture_image(outSmoothedImage, inTextureImage);
    gdImagePtr outSepiaImage = sepia_image(outTexturedImage);

    write_jpeg_file(outSepiaImage, outFileName);

    gdImageDestroy(outContrastImage);
    gdImageDestroy(outSmoothedImage);
    gdImageDestroy(outTexturedImage);
    gdImageDestroy(outSepiaImage);
    gdImageDestroy(inImage);
}

/**
 * thread_function
 * Argumentos: arg (void*)
 * Retorna: void*
 * Objetivo: função executada por cada thread para processar as imagens
 */
void *thread_function(void *arg) {
    int threadId = *((int *)arg);
    clock_gettime(CLOCK_MONOTONIC, &threadStartTimes[threadId]);

    while (1) {
        int idx;
        ssize_t n = read(pipefd[0], &idx, sizeof(idx));
        if (n != sizeof(idx)) {
            break;
        }
        process_image(images[idx].filename, images[idx].fullPathName,
                      inTextureImage, outputDir);
        pthread_mutex_lock(&processedCountMutex);
        processedCount++;
        pthread_mutex_unlock(&processedCountMutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &threadEndTimes[threadId]);
    return NULL;
}

/**
 * keyboard_function
 * Argumentos: arg (void*)
 * Retorna: void*
 * Objetivo: capturar entradas do teclado e apresentar tempo médio de processamento/ nr de imgs processadas
 */
void *keyboard_function(void *arg) {
    (void)arg;

    while (!done) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        } else if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            int c = getchar();
            if (c == EOF) {
                break;
            }
            if (c == 'S' || c == 's') {
                pthread_mutex_lock(&processedCountMutex);
                int currentCount = processedCount;
                pthread_mutex_unlock(&processedCountMutex);

                if (currentCount == 0) {
                    fprintf(stderr,
                            "[INFO] Já foram processadas %d / %d imagens. (Ainda nenhuma imagem processada)\n",
                            currentCount, nnFiles);
                } else {
                    struct timespec currentTime;
                    clock_gettime(CLOCK_MONOTONIC, &currentTime);
                    struct timespec elapsedParallel = diff_timespec(&currentTime, &startTimePar);
                    double elapsedSeconds = elapsedParallel.tv_sec + (elapsedParallel.tv_nsec / 1e9);
                    double averageTimePerImage = elapsedSeconds / currentCount;

                    fprintf(stderr,
                            "[INFO] Já foram processadas %d / %d imagens.\n"
                            "       Tempo médio por imagem: %.6f s\n",
                            currentCount, nnFiles, averageTimePerImage);
                }
            }
        }
    }

    return NULL;
}

/**
 * main
 * Argumentos: argc (int), argv (char**)
 * Retorna: int
 * Objetivo: ponto de entrada do programa, gere argumentos e cria threads de processamento
 */
int main(int argc, char* argv[]) {
    char *dirPath;
    struct dirent *dirEntry;
    struct ImageInfo *temp;
    size_t capacity = 0;

    struct timespec parTime, seqTime, totalTime;

    clock_gettime(CLOCK_MONOTONIC, &startTimeTotal);
    clock_gettime(CLOCK_MONOTONIC, &startTimeSeq);

    if (argc < 4) {
        fprintf(stderr, "Uso: <caminho-directoria> <n_threads> <-name|-size>\n");
        exit(1);
    }

    dirPath = argv[1];
    if (sscanf(argv[2], "%d", &threadCount) != 1) {
        threadCount = 1;
    }
    if (threadCount <= 0) {
        threadCount = 1;
    }

    char sortKind[16];
    strncpy(sortKind, argv[3], sizeof(sortKind) - 1);
    sortKind[sizeof(sortKind) - 1] = '\0';
    string_tolower(sortKind);

    DIR *d = opendir(dirPath);
    if (!d) {
        fprintf(stderr, "Não foi possível abrir a directoria: %s\n", dirPath);
        exit(1);
    }

    nnFiles = 0;
    temp = NULL;

    while ((dirEntry = readdir(d)) != NULL) {
        if (dirEntry->d_type == DT_REG) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, dirEntry->d_name);

            struct stat st;
            if (stat(fullPath, &st) == 0) {
                const char *extension = strrchr(dirEntry->d_name, '.');
                if (extension != NULL &&
                    (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0)) {

                    if (nnFiles == (int)capacity) {
                        if (capacity == 0) {
                            capacity = 16;
                        } else {
                            capacity *= 2;
                        }
                        temp = realloc(images, capacity * sizeof(struct ImageInfo));
                        if (!temp) {
                            fprintf(stderr, "Falha na alocação de memória\n");
                            closedir(d);
                            free(images);
                            exit(1);
                        }
                        images = temp;
                    }
                    images[nnFiles].filename = strdup(dirEntry->d_name);
                    images[nnFiles].fullPathName = strdup(fullPath);
                    images[nnFiles].size = st.st_size;
                    nnFiles++;
                }
            }
        }
    }
    closedir(d);

    if (nnFiles == 0) {
        free(images);
        exit(0);
    }

    if (strcmp(sortKind, "-name") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    } else if (strcmp(sortKind, "-size") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_size);
    } else {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    }

    inTextureImage = read_png_file("./paper-texture.png");

    snprintf(outputDir, sizeof(outputDir), "%s/old_photo_PAR_B", dirPath);
    {
        size_t len = strlen(outputDir);
        if (len + 1 < sizeof(outputDir)) {
            if (len > 0 && outputDir[len - 1] != '/') {
                strncat(outputDir, "/", sizeof(outputDir) - len - 1);
            }
        } else {
            fprintf(stderr, "Caminho do diretório de saída muito longo.\n");
            exit(1);
        }
    }

    if (!create_directory(outputDir)) {
        fprintf(stderr, "Não foi possível criar a directoria de saída %s\n", outputDir);
    }

    clock_gettime(CLOCK_MONOTONIC, &endTimeSeq);
    clock_gettime(CLOCK_MONOTONIC, &startTimePar);

    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(1);
    }

    threads = malloc(sizeof(pthread_t) * threadCount);
    threadIds = malloc(sizeof(int) * threadCount);
    threadStartTimes = malloc(sizeof(struct timespec) * threadCount);
    threadEndTimes = malloc(sizeof(struct timespec) * threadCount);

    if (!threads || !threadIds || !threadStartTimes || !threadEndTimes) {
        fprintf(stderr, "Falha na alocação de memória para threads\n");
        free(threads);
        free(threadIds);
        free(threadStartTimes);
        free(threadEndTimes);
        for (int i = 0; i < nnFiles; i++) {
            free(images[i].filename);
            free(images[i].fullPathName);
        }
        free(images);
        if (inTextureImage) gdImageDestroy(inTextureImage);
        exit(1);
    }

    for (int i = 0; i < threadCount; i++) {
        threadIds[i] = i;
        pthread_create(&threads[i], NULL, thread_function, &threadIds[i]);
    }

    pthread_create(&keyboardThread, NULL, keyboard_function, NULL);

    for (int i = 0; i < nnFiles; i++) {
        int idx = i;
        ssize_t n = write(pipefd[1], &idx, sizeof(idx));
        if (n != sizeof(idx)) {
            break;
        }
    }

    close(pipefd[1]);

    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }

    done = 1;
    pthread_join(keyboardThread, NULL);
    close(pipefd[0]);

    if (inTextureImage) gdImageDestroy(inTextureImage);

    for (int i = 0; i < nnFiles; i++) {
        free(images[i].filename);
        free(images[i].fullPathName);
    }
    free(images);
    free(threads);
    free(threadIds);

    clock_gettime(CLOCK_MONOTONIC, &endTimePar);
    clock_gettime(CLOCK_MONOTONIC, &endTimeTotal);

    parTime = diff_timespec(&endTimePar, &startTimePar);
    seqTime = diff_timespec(&endTimeSeq, &startTimeSeq);
    totalTime = diff_timespec(&endTimeTotal, &startTimeTotal);

    {
        char timingFileName[2048];
        if (strcmp(sortKind, "-name") == 0) {
            snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-name.txt", outputDir, threadCount);
        } else {
            snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-size.txt", outputDir, threadCount);
        }

        FILE *fout = fopen(timingFileName, "w");
        if (fout) {
            fprintf(fout, "Tempo sequencial: %jd.%09ld s\n", (intmax_t)seqTime.tv_sec, seqTime.tv_nsec);
            fprintf(fout, "Tempo paralelo: %jd.%09ld s\n", (intmax_t)parTime.tv_sec, parTime.tv_nsec);
            fprintf(fout, "Tempo total: %jd.%09ld s\n", (intmax_t)totalTime.tv_sec, totalTime.tv_nsec);

            for (int i = 0; i < threadCount; i++) {
                struct timespec threadDiff = diff_timespec(&threadEndTimes[i], &threadStartTimes[i]);
                fprintf(fout, "Thread %d tempo: %jd.%09ld s\n", i, (intmax_t)threadDiff.tv_sec, threadDiff.tv_nsec);
            }
            fclose(fout);
        }
    }

    free(threadStartTimes);
    free(threadEndTimes);

    return 0;
}