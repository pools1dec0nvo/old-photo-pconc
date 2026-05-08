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
#include "image-lib.h"

/***********************************************************************
 * old-photo-parallel-A
 * Carolina João (109587) e Tiago Lopes Carvalho (106396)
 * Programção Concorrente - LEEC 2024/25
 * 
 * Especiais agradecimentos ao professor JNOS.
 *
 * "If you have any trouble sounding condescending, find a Unix user to show you how it's done" - Scott Adams
 * Nota: Nenhuma máquina Windows prejudicou o nosso desenvolvimento :D
 ***********************************************************************/

struct ImageInfo {
    char *filename;      // Nome do ficheiro (por exemplo: "imagem.jpg")
    char *fullPathName;  // Caminho completo do ficheiro (por exemplo: "/path/to/dir/imagem.jpg")
    off_t size;
};

struct ThreadArgs {
    int threadId;
    int startIdx;
    int endIdx; 
};

struct ImageInfo *images = NULL;
int nnFiles = 0;
gdImagePtr inTextureImage = NULL;
int threadCount = 1;

/* Vetores para medição do tempo */
struct timespec *threadStartTimes = NULL;
struct timespec *threadEndTimes = NULL;

/* Diretório de saída global */
char outputDir[1024];

/* Função auxiliar para tornar strings minúsculas */
void string_tolower(char *str) {
    while (*str) {
        *str = (char)tolower((unsigned char)*str);
        str++;
    }
}

/* Funções de comparação para ordenação */
int cmp_name(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    return strcmp(ia->filename, ib->filename);
}

int cmp_size(const void *a, const void *b) {
    const struct ImageInfo *ia = (const struct ImageInfo *)a;
    const struct ImageInfo *ib = (const struct ImageInfo *)b;
    if (ia->size < ib->size) return -1;
    else if (ia->size > ib->size) return 1;
    return 0;
}

/* Função para processar uma imagem individual */
void process_image(const char *filename, const char *fullPathName, gdImagePtr inTextureImage, const char *outputDir) {
    char outFileName[1024];

    // Constrói o nome completo do ficheiro de saída (outputDir deve terminar com '/')
    snprintf(outFileName, sizeof(outFileName), "%s%s", outputDir, filename);

    // Verifica se a imagem já foi processada anteriormente
    if (access(outFileName, F_OK) != -1) {
        return;
    }

    // Lê a imagem original a partir do caminho completo
    gdImagePtr inImage = read_jpeg_file((char *)fullPathName);
    if (inImage == NULL) {
        return;
    }

    // Aplica as transformações à imagem
    gdImagePtr outContrastImage = contrast_image(inImage);
    gdImagePtr outSmoothedImage = smooth_image(outContrastImage);
    gdImagePtr outTexturedImage = texture_image(outSmoothedImage, inTextureImage);
    gdImagePtr outSepiaImage = sepia_image(outTexturedImage); 

    // Escreve a imagem resultante no ficheiro de saída
    write_jpeg_file(outSepiaImage, outFileName);

    // Liberta a memória das imagens temporárias
    gdImageDestroy(outSmoothedImage);
    gdImageDestroy(outSepiaImage);
    gdImageDestroy(outContrastImage);
    gdImageDestroy(outTexturedImage);
    gdImageDestroy(inImage);
}

/* Função executada por cada thread */
void *thread_function(void *arg) {
    struct ThreadArgs *targs = (struct ThreadArgs *)arg;

    // Regista o tempo de início da thread
    clock_gettime(CLOCK_MONOTONIC, &threadStartTimes[targs->threadId]);

    // Processa as imagens atribuídas a esta thread
    for (int i = targs->startIdx; i < targs->endIdx; i++) {
        process_image(images[i].filename, images[i].fullPathName, inTextureImage, outputDir);
    }

    // Regista o tempo de fim da thread
    clock_gettime(CLOCK_MONOTONIC, &threadEndTimes[targs->threadId]);

    return NULL;
}

int main(int argc, char* argv[]) {
    char *dirPath;
    struct dirent *dirEntry;
    struct ImageInfo *temp;

    struct timespec startTimeTotal, endTimeTotal;
    struct timespec startTimeSeq, endTimeSeq;
    struct timespec startTimePar, endTimePar;
    
    // Marca o início do tempo total e do tempo sequencial
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
    strncpy(sortKind, argv[3], sizeof(sortKind)-1);
    sortKind[sizeof(sortKind)-1] = '\0';
    string_tolower(sortKind);

    DIR *d = opendir(dirPath);
    if (!d) {
        fprintf(stderr, "Não foi possível abrir a directoria: %s\n", dirPath);
        exit(1);
    }

    temp = NULL;
    size_t capacity = 0;
    nnFiles = 0;

    // Lê os ficheiros na directoria, filtrando apenas ficheiros .jpg ou .jpeg
    while ((dirEntry = readdir(d)) != NULL) {
        if (dirEntry->d_type == DT_REG) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, dirEntry->d_name);

            struct stat st;
            if (stat(fullPath, &st) == 0) {
                const char *extension = strrchr(dirEntry->d_name, '.');

                // Verifica se o ficheiro tem extensão .jpeg ou .jpg
                if (extension != NULL && 
                   (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0)) {

                    // Expande a capacidade do array 'images' se necessário
                    if (nnFiles == (int)capacity) {
                        if (capacity == 0) {
                            capacity = 16;
                        } else {
                            capacity = capacity * 2;
                        }

                        temp = realloc(images, capacity * sizeof(struct ImageInfo));
                        if (temp == NULL) {
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

    // Ordena as imagens de acordo com o critério especificado
    if (strcmp(sortKind, "-name") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    } else if (strcmp(sortKind, "-size") == 0) {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_size);
    } else {
        qsort(images, nnFiles, sizeof(struct ImageInfo), cmp_name);
    }

    // Carrega a imagem de textura
    inTextureImage = read_png_file("./paper-texture.png");

    // Cria o caminho do diretório de saída dentro da directoria de entrada
    snprintf(outputDir, sizeof(outputDir), "%s/old_photo_PAR_A", dirPath);

    // Verifica se é possível adicionar a barra final ao path
    {
        size_t len = strlen(outputDir);
        if (len + 1 < sizeof(outputDir)) {
            // Adiciona '/' se não existir no final
            if (len > 0 && outputDir[len-1] != '/') {
                strncat(outputDir, "/", sizeof(outputDir) - len - 1);
            }
        } else {
            fprintf(stderr, "Caminho do diretório de saída muito longo.\n");
            exit(1);
        }
    }

    // Cria o diretório de saída
    if (!create_directory(outputDir)) {
        fprintf(stderr, "Não foi possível criar a directoria de saída %s\n", outputDir);
    }

    // Marca o fim da parte sequencial e o início da parte paralela
    clock_gettime(CLOCK_MONOTONIC, &endTimeSeq);
    clock_gettime(CLOCK_MONOTONIC, &startTimePar);

    // Alocação de memória para threads e estruturas auxiliares
    pthread_t *threads = malloc(sizeof(pthread_t) * threadCount);
    struct ThreadArgs *targs = malloc(sizeof(struct ThreadArgs) * threadCount);
    threadStartTimes = malloc(sizeof(struct timespec) * threadCount);
    threadEndTimes = malloc(sizeof(struct timespec) * threadCount);

    if (!threads || !targs || !threadStartTimes || !threadEndTimes) {
        fprintf(stderr, "Falha na alocação de memória para threads\n");
        free(threads);
        free(targs);
        free(threadStartTimes);
        free(threadEndTimes);
        for (int i = 0; i < nnFiles; i++) {
            free(images[i].filename);
            free(images[i].fullPathName);
        }
        free(images);
        if (inTextureImage) 
            gdImageDestroy(inTextureImage);
        exit(1);
    }

    int currentIndex = 0;
    // Divide os ficheiros entre as threads
    for (int i = 0; i < threadCount; i++) {
        int chunk = nnFiles / threadCount;
        int remainder = nnFiles % threadCount;
        if (i < remainder) {
            chunk += 1;
        }
        targs[i].threadId = i;
        targs[i].startIdx = currentIndex;
        targs[i].endIdx = currentIndex + chunk;
        currentIndex += chunk;
    }

    // Criação das threads
    for (int i = 0; i < threadCount; i++) {
        pthread_create(&threads[i], NULL, thread_function, &targs[i]);
    }

    // Espera que todas as threads terminem
    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }

    // Limpeza da memória alocada
    if (inTextureImage) gdImageDestroy(inTextureImage);
    for (int i = 0; i < nnFiles; i++) {
        free(images[i].filename);
        free(images[i].fullPathName);
    }
    free(images);
    free(threads);
    free(targs);

    // Marca o fim da parte paralela e o fim do tempo total
    clock_gettime(CLOCK_MONOTONIC, &endTimePar);
    clock_gettime(CLOCK_MONOTONIC, &endTimeTotal);

    // Calcula os tempos sequencial, paralelo e total
    struct timespec parTime = diff_timespec(&endTimePar, &startTimePar);
    struct timespec seqTime = diff_timespec(&endTimeSeq, &startTimeSeq);
    struct timespec totalTime = diff_timespec(&endTimeTotal, &startTimeTotal);

    char timingFileName[2048];
    // Constrói o nome do ficheiro de tempos
    if (strcmp(sortKind, "-name") == 0) {
        snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-name.txt", outputDir, threadCount);
    } else {
        snprintf(timingFileName, sizeof(timingFileName), "%stiming_%d-size.txt", outputDir, threadCount);
    }

    // Escreve os tempos num ficheiro
    FILE *fout = fopen(timingFileName, "w");
    if (fout) {
        fprintf(fout, "Tempo sequencial: %jd.%09ld s\n", (intmax_t)seqTime.tv_sec, seqTime.tv_nsec);
        fprintf(fout, "Tempo paralelo: %jd.%09ld s\n", (intmax_t)parTime.tv_sec, parTime.tv_nsec);
        fprintf(fout, "Tempo total: %jd.%09ld s\n", (intmax_t)totalTime.tv_sec, totalTime.tv_nsec);

        // Tempo individual de cada thread
        for (int i = 0; i < threadCount; i++) {
            struct timespec threadDiff = diff_timespec(&threadEndTimes[i], &threadStartTimes[i]);
            fprintf(fout, "Thread %d tempo: %jd.%09ld s\n", i, (intmax_t)threadDiff.tv_sec, threadDiff.tv_nsec);
        }

        fclose(fout);
    }

    free(threadStartTimes);
    free(threadEndTimes);

    return 0;
}
