#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <unistd.h>

typedef struct shd_mem {
    int *MATRIX;
    int *VECTOR;
    int ch_num;
    pid_t *children;
    int *starts;
    int *ends;
} shd_mem;

void Print2DMatrix(int *target, int n_size, int m_size);

void VerifyOutput(const int *correct, const int *output, int size);

void ReadMatrix(const char *matrix_fn, shd_mem *shd_matrix, int *matrix_size_n, int *matrix_size_m);

void ReadVector(const char *vector_fn, int matrix_size_n, shd_mem *shd_matrix);

void PrintVector(int size, int *target);

int main(int argc, char **argv) {

    if (argc < 4) {
        printf("Требуется указать в формате [matrix_mult <матрица> <вектор> <кол-во процессов>]");
        exit(1);
    }

    const char *matrix_fn = argv[1];
    const char *vector_fn = argv[2];

    int matrix_size_n = 0;
    int matrix_size_m = 0;
    int thread_num = atoi(argv[3]);

    int *C_parent = NULL;
    int sum, i, j, e;
    int order = 0;
    int start = 0, end = 0;

    pid_t pid;
    int shmid;
    int shm_2d;
    int *output;

    void *shared_memory;
    shd_mem *shd_matrix;

    if (thread_num > 0) {
        //Выделяем сегмент раздел. памятт
        shmid = shmget(IPC_PRIVATE, sizeof(shd_mem) * 1, IPC_CREAT | 0666);

        //Присоед. сегмент
        shared_memory = (int *) shmat(shmid, NULL, 0);
        shd_matrix = (shd_mem *) shared_memory;

        //Читаем матрицу и вектор из файлов
        ReadMatrix(matrix_fn, shd_matrix, &matrix_size_n, &matrix_size_m);
        ReadVector(vector_fn, matrix_size_m, shd_matrix);

        // выделяем общую память для результата
        shm_2d = shmget(IPC_PRIVATE, matrix_size_m * sizeof(int), IPC_CREAT | 0666);
        output = (int *) shmat(shm_2d, NULL, 0);

        shd_matrix->ch_num = thread_num;
        shd_matrix->children = (pid_t *) malloc(sizeof(pid_t) * thread_num);
        shd_matrix->starts = (int *) malloc(sizeof(pid_t) * thread_num);
        shd_matrix->ends = (int *) malloc(sizeof(pid_t) * thread_num);

        pid_t parent;
        parent = getpid();
        for (i = 0; i < thread_num; i++) {
            if (getpid() == parent) {
                pid = fork();
                shd_matrix->children[i] = getpid();
                int start_idx = i * (matrix_size_n / (thread_num));
                shd_matrix->starts[i] = start_idx;
                shd_matrix->ends[i] = start_idx + (matrix_size_n / (thread_num));
            }
        }

        //error occurred
        if (pid < 0) {
            fprintf(stderr, "Fork Failed");
            return 1;
        }
            //child process
        else if (pid == 0) {
            // Map input memory
            shared_memory = (int *) shmat(shmid, NULL, 0);
            if (shared_memory == (char *) (-1))
                perror("shmat");

            // Map output memory
            shd_matrix = (shd_mem *) shared_memory;
            output = (int *) shmat(shm_2d, NULL, 0);

            for (i = 0; i < shd_matrix->ch_num; i++) {
                if (shd_matrix->children[i] == getpid()) {
                    order = i;
                    start = shd_matrix->starts[i];
                    end = shd_matrix->ends[i];
                    break;
                }
            }

            printf("Child process (%d)...\n", order);
            //start = order * (matrix_size_n / (thread_num));
            printf("start %d\n", start);
            // end = start + (matrix_size_n / (thread_num));
            printf("end %d\n", end);

            for (i = start; i < end; i++) {
                sum = 0;
                for (e = 0; e < matrix_size_m; e++) {
                    sum += shd_matrix->MATRIX[e + i * matrix_size_n] * shd_matrix->VECTOR[e];
                    //printf("%d * %d\n",shd_matrix->MATRIX[e + j * matrix_size_n], shd_matrix->VECTOR[e]);
                }
                //printf("%d = %d\n", i, sum);
                output[i] = sum;
            }
        }
            //parent process
        else {
            usleep(15000);

            // wait completion of children
            wait(&parent);
            C_parent = (int *) malloc(matrix_size_m * sizeof(int));

            for (i = 0; i < matrix_size_n; i++) {
                sum = 0;
                for (e = 0; e < matrix_size_m; e++) {
                    sum += shd_matrix->MATRIX[e + i * matrix_size_n] * shd_matrix->VECTOR[e];
                }
                C_parent[i] = sum;
            }
            PrintVector(matrix_size_n, C_parent);

            // Print result from parent
            printf("\nInput matrix: \n");
            Print2DMatrix(shd_matrix->MATRIX, matrix_size_n, matrix_size_m);

            printf("Input vector: \n");
            PrintVector(matrix_size_m, shd_matrix->VECTOR);

            VerifyOutput(C_parent, output, matrix_size_n);

            printf("Output vector \n");
            PrintVector(matrix_size_n, output);

            //Отчистка
            if (shmdt(shared_memory) == -1) {
                fprintf(stderr, "shmdt failed\n");
                exit(EXIT_FAILURE);
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1) {
                fprintf(stderr, "shmctl(IPC_RMID) failed\n");
                exit(EXIT_FAILURE);
            }

            if (shmdt(output) == -1) {
                fprintf(stderr, "shmdt failed\n");
                exit(EXIT_FAILURE);
            }

            if (shmctl(shm_2d, IPC_RMID, 0) == -1) {
                fprintf(stderr, "shmctl(IPC_RMID) failed\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    return 0;
}

void PrintVector(int size, int *target) {
    for (int i = 0; i < size; i++) {
        printf("%d\n", target[i]);
    }
    printf("\n");
}

void ReadVector(const char *vector_fn, int matrix_size_n, shd_mem *shd_matrix) {
    FILE *file;
    file = fopen(vector_fn, "r");
    if (file == NULL) {
        printf("Не удалось открыть файл %s\n", vector_fn);
        exit(1);
    }
    shd_matrix->VECTOR = (int *) malloc(matrix_size_n * sizeof(int));

    int k;
    for (k = 0; k < matrix_size_n && !feof(file); k++) {
        fscanf(file, "%d", &shd_matrix->VECTOR[k]);
    }

    fclose(file);
}

void ReadMatrix(const char *matrix_fn, shd_mem *shd_matrix, int *matrix_size_n, int *matrix_size_m) {
    FILE *file;
    file = fopen(matrix_fn, "r");
    if (file == NULL) {
        printf("Не удалось открыть файл %s\n", matrix_fn);
        exit(1);
    }
    fscanf(file, "%d %d", matrix_size_n, matrix_size_m);
    shd_matrix->MATRIX = (int *) malloc((*matrix_size_n) * (*matrix_size_m) * sizeof(int));

    int k, l;
    for (k = 0; k < (*matrix_size_n) && !feof(file); k++) {
        for (l = 0; l < (*matrix_size_m) && !feof(file); l++) {
            fscanf(file, "%d", &shd_matrix->MATRIX[k * (*matrix_size_n) + l]);
        }
    }
    fclose(file);
}


void Print2DMatrix(int *target, int n_size, int m_size) {
    int i, j;

    if (n_size >= 1) {
        for (i = 0; i < n_size; i++) {
            for (j = 0; j < m_size; j++)
                printf("%d\t", target[i * n_size + j]);
            printf("\n");
        }
        printf("\n");
    }
}

void VerifyOutput(const int *correct, const int *output, int size) {
    int fail = 0;
    for (int i = 0; i < size; i++) {
        if ((output[i] - correct[i]) != 0 && fail == 0) {
            fail = 1;
        }
    }

    if (fail == 1) {
        printf("Неверный результат!\n");
    } else {
        printf("Верный результат!\n");
    }
}