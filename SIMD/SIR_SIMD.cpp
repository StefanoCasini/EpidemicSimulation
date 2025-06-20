#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include "lib/cJSON.h"
#include <emmintrin.h>
#include <immintrin.h>
#include <random>
#include <unistd.h>

#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

int *N;      // Indici dell'inizio dei vicini per ogni nodo
int *L;      // Lista di adiacenza compressa
int *Levels; // Momento dell'infezione: istante in cui viene infettato
int *Immune; // Stato di immunità passa da bool a int

#define TRUE -1
#define FALSE 0
#define AVX_DATALANE 32

int num_nodes;
int num_edges;

double cpuSecond()
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((double)ts.tv_sec + (double)ts.tv_nsec * 1.e-9);
}

char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("Error opening file!\n");
        return NULL;
    }

    char *json_string = NULL;
    size_t size = 0;
    size_t capacity = 128; // Initial buffer size
    json_string = (char *)malloc(capacity);
    if (!json_string)
    {
        printf("Memory allocation failed!\n");
        fclose(file);
        return NULL;
    }

    int ch;
    while ((ch = fgetc(file)) != EOF)
    {
        json_string[size++] = (char)ch;
        // Resize buffer if needed
        if (size >= capacity - 1)
        {
            capacity *= 2; // Double the buffer size
            json_string = (char *)realloc(json_string, capacity);
            if (!json_string)
            {
                printf("Memory reallocation failed!\n");
                fclose(file);
                return NULL;
            }
        }
    }
    json_string[size] = '\0'; // Null-terminate the string
    fclose(file);
    return json_string;
}

void import_network(const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "../GRAPH_GENERATOR/%s", filename);
    char *json_string = read_file(filepath);
    if (!json_string)
    {
        exit(1);
    }

    cJSON *root = cJSON_Parse(json_string);
    free(json_string); // Free memory after parsing
    if (!root)
    {
        printf("Error parsing JSON!\n");
        return;
    }

    cJSON *json_numNodes = cJSON_GetObjectItem(root, "num_nodes");
    cJSON *json_numEdges = cJSON_GetObjectItem(root, "num_edges");
    num_nodes = json_numNodes->valueint;
    num_edges = json_numEdges->valueint;

    // Extract arrays
    cJSON *json_N = cJSON_GetObjectItem(root, "N");
    cJSON *json_L = cJSON_GetObjectItem(root, "L");

    int size_N = cJSON_GetArraySize(json_N);
    int size_L = cJSON_GetArraySize(json_L);

    // N = (int *)malloc(size_N * sizeof(int));
    // L = (int *)malloc(size_L * sizeof(int));

    // Levels = (int *)malloc(num_nodes * sizeof(int));
    // Immune = (int *)malloc(num_nodes * sizeof(int));

    N = (int *)_mm_malloc(size_N * sizeof(int), AVX_DATALANE);
    L = (int *)_mm_malloc(size_L * sizeof(int), AVX_DATALANE);

    Levels = (int *)_mm_malloc(num_nodes * sizeof(int), AVX_DATALANE);
    Immune = (int *)_mm_malloc(num_nodes * sizeof(int), AVX_DATALANE);

    for (int i = 0; i < size_N; i++)
    {
        N[i] = cJSON_GetArrayItem(json_N, i)->valueint;
    }
    for (int i = 0; i < size_L; i++)
    {
        L[i] = cJSON_GetArrayItem(json_L, i)->valueint;
    }

    for (int i = 0; i < num_nodes; i++)
    {
        Levels[i] = -1;    // Non infetto
        Immune[i] = FALSE; // Non immune
    }
    Levels[0] = 0; // Nodo inizialmente infetto al tempo 0
}

void print_network()
{
    printf("Network:\n");
    for (int i = 0; i < num_nodes; i++)
    {
        printf("%d: ", i);
        for (int j = N[i]; j < N[i + 1]; j++)
        {
            printf("%d ", L[j]);
        }
        printf("\n");
    }
    printf("\n");
}

void print_status(int step, int active_infections)
{
    printf("Step %d: %d active infections\n", step, active_infections);
    if (active_infections > 0)
    {
        printf("Infected nodes: \n");
        for (int i = 0; i < num_nodes; i++)
        {
            if (Levels[i] == step)
            {
                printf("%d ", i);
            }
            else
            {
            }
        }
        printf("\n");
        /*
        printf("Other Nodes: \n");
        for (int i = 0; i < num_nodes; i++)
        {
            if (Levels[i] != step)
            {
                printf("%d ", i);
            }
        }
        printf("\n");
        */
    }
}

void print__mm_register_ps(__m256 reg)
{
    float values[8];
    _mm256_storeu_ps(values, reg); // Memorizza il registro in un array
    for (int i = 0; i < 8; i++)
    {
        printf("XMM[%d]: %f\n", i, values[i]);
    }
}

void print__mm_register_epi32(__m256i reg)
{
    printf("XMM[7]: %d\n", _mm256_extract_epi32(reg, 7));
    printf("XMM[6]: %d\n", _mm256_extract_epi32(reg, 6));
    printf("XMM[5]: %d\n", _mm256_extract_epi32(reg, 5));
    printf("XMM[4]: %d\n", _mm256_extract_epi32(reg, 4));
    printf("XMM[3]: %d\n", _mm256_extract_epi32(reg, 3));
    printf("XMM[2]: %d\n", _mm256_extract_epi32(reg, 2));
    printf("XMM[1]: %d\n", _mm256_extract_epi32(reg, 1));
    printf("XMM[0]: %d\n", _mm256_extract_epi32(reg, 0));
}

// __m256 random_m256_01() {
//     alignas(32) float values[8]; // Ensure proper alignment for AVX
//     std::random_device rd;  // Random seed
//     std::mt19937 gen(rd()); // Mersenne Twister RNG
//     std::uniform_real_distribution<float> dist(0.0f, 1.0f);

//     for (int i = 0; i < 8; i++) {
//         values[i] = dist(gen); // Generate random float in [0,1]
//     }

//     return _mm256_load_ps(values); // Load values into an AVX register
// }

void simulate(double p, double q)
{
    int active_infections = 1;
    int step = 0;

    __m256 probability = _mm256_set1_ps(p);
    __m256i minus1 = _mm256_set1_epi32(-1);
    __m256i zeros = _mm256_set1_epi32(0);

    //print_status(step, active_infections);

    while (active_infections > 0)
    {
        for (int i = 0; i < num_nodes; i++)
        {
            if (Levels[i] == step && Immune[i] == FALSE) // controllo per il nodo 0
            {                                            // Nodo infetto al passo corrente
                int num_neighbors = N[i + 1] - N[i];
                int remainder = num_neighbors % 8;
                // printf("num_neighbors: %d, remainder: %d\n", num_neighbors, remainder);

                __m256i neighbors;

                for (int j = 0; j < num_neighbors; j += 8)
                {

                    neighbors = _mm256_loadu_si256((__m256i *)&L[N[i] + j]);

                    if ((j + 8) > num_neighbors && remainder > 0)
                    { // ultimo vettore
                        __m256i mask = _mm256_set_epi32(
                            0,
                            remainder > 6 ? -1 : 0,
                            remainder > 5 ? -1 : 0,
                            remainder > 4 ? -1 : 0,
                            remainder > 3 ? -1 : 0,
                            remainder > 2 ? -1 : 0,
                            remainder > 1 ? -1 : 0,
                            -1);
                        neighbors = _mm256_and_si256(neighbors, mask);
                    }

                    // printf("Neighbors after mask: \n");
                    // print__mm_register_epi32(neighbors);

                    __m256i Levels_SIMD = _mm256_i32gather_epi32(Levels, neighbors, 4);
                    __m256i Immune_SIMD = _mm256_i32gather_epi32(Immune, neighbors, 4);

                    __m256i mask_susceptible = _mm256_cmpeq_epi32(Levels_SIMD, minus1);
                    __m256i mask_not_immune = _mm256_cmpeq_epi32(Immune_SIMD, zeros);
                    __m256i infection_mask = _mm256_and_si256(mask_susceptible, mask_not_immune);

                    // printf("Infection Mask: \n");
                    // print__mm_register_epi32(infection_mask);

                    // __m256 random = random_m256_01();
                    __m256 random = _mm256_set_ps(
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX,
                        (float)rand() / RAND_MAX);

                    __m256 infection_probability = _mm256_cmp_ps(random, probability, _CMP_LT_OS);
                    // printf("Infection Probability: \n");
                    // print__mm_register_ps(infection_probability);
                    __m256i final_mask = _mm256_and_si256(_mm256_castps_si256(infection_probability), infection_mask);
                    __m256i neighbors_infected = _mm256_and_si256(neighbors, final_mask);
                    // printf("FinalMask: \n");
                    // print__mm_register_epi32(final_mask);
                    // printf("Neighbors Infected: \n");
                    // print__mm_register_epi32(neighbors_infected);
                    //_mm256_maskstore_epi32(Levels, neighbors_infected, _mm256_set1_epi32(step + 1));

                    alignas(32) int indices[8];                                 // Store extracted indices
                    _mm256_store_si256((__m256i *)indices, neighbors_infected); // Extract all at once

                    if (indices[0] != 0)
                    {
                        Levels[indices[0]] = step + 1;
                        active_infections++;
                    }
                    if (indices[1] != 0)
                    {
                        Levels[indices[1]] = step + 1;
                        active_infections++;
                    }
                    if (indices[2] != 0)
                    {
                        Levels[indices[2]] = step + 1;
                        active_infections++;
                    }
                    if (indices[3] != 0)
                    {
                        Levels[indices[3]] = step + 1;
                        active_infections++;
                    }
                    if (indices[4] != 0)
                    {
                        Levels[indices[4]] = step + 1;
                        active_infections++;
                    }
                    if (indices[5] != 0)
                    {
                        Levels[indices[5]] = step + 1;
                        active_infections++;
                    }
                    if (indices[6] != 0)
                    {
                        Levels[indices[6]] = step + 1;
                        active_infections++;
                    }
                    if (indices[7] != 0)
                    {
                        Levels[indices[7]] = step + 1;
                        active_infections++;
                    }
                }
            }

            if (Levels[i] == step && ((double)rand() / RAND_MAX) < q)
            {
                Immune[i] = TRUE; // Nodo recuperato
                active_infections--;
            }
            else if (Levels[i] == step)
            {
                Levels[i] = step + 1; // Nodo può infettare anche al prossimo step
            }
        }
        step++;
        //print_status(step, active_infections);
        // if(step == 1){
        //     return;
        // }
    }

    // FREE
    _mm_free(N);
    _mm_free(L);
    _mm_free(Levels);
    _mm_free(Immune);
}

int main(int argc, char *argv[])
{
    // Selezionando p=1 e q=1 otteniamo una ricerca in ampiezza
    double p = 1; // Probabilità di infezione
    double q = 1; // Probabilità di guarigione

    double start_import = cpuSecond();
    import_network(argv[1]);
    double end_import = cpuSecond();
    printf("Import time: %f seconds\n", end_import - start_import);

    printf("Simulating with p=%f, q=%f\n", p, q);
    double start = cpuSecond();
    simulate(p, q);
    double end = cpuSecond();
    printf("Elapsed time: %f seconds\n", end - start);

    return 0;
}