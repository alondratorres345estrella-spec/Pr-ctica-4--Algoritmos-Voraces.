#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHARS 256
#define MAX_TREE_HT 256

typedef struct Nodo {
    unsigned char caracter;
    unsigned int frecuencia;
    int es_hoja;
    struct Nodo *izq, *der;
} Nodo;

typedef struct {
    unsigned int tamano;
    Nodo *datos[MAX_CHARS];
} MinHeap;

typedef struct {
    uint8_t *buffer;
    size_t bit_pos;
    size_t capacidad_bytes;
} BitStream;

/* --- Funciones auxiliares del árbol y desempate determinista --- */

unsigned char min_caracter(Nodo *nodo) {
    if (nodo->es_hoja) return nodo->caracter;
    unsigned char a = min_caracter(nodo->izq);
    unsigned char b = min_caracter(nodo->der);
    return (a < b) ? a : b;
}

int es_menor(Nodo *a, Nodo *b) {
    if (a->frecuencia != b->frecuencia)
        return a->frecuencia < b->frecuencia;
    return min_caracter(a) < min_caracter(b);
}

Nodo *crear_nodo(unsigned char caracter, unsigned int frecuencia, int es_hoja,
                 Nodo *izq, Nodo *der) {
    Nodo *nuevo = (Nodo *)malloc(sizeof(Nodo));
    nuevo->caracter = caracter;
    nuevo->frecuencia = frecuencia;
    nuevo->es_hoja = es_hoja;
    nuevo->izq = izq;
    nuevo->der = der;
    return nuevo;
}

void liberar_arbol(Nodo *raiz) {
    if (!raiz) return;
    liberar_arbol(raiz->izq);
    liberar_arbol(raiz->der);
    free(raiz);
}

/* --- Cola de prioridad (Min-Heap) --- */

void swap_nodos(Nodo **a, Nodo **b) {
    Nodo *tmp = *a;
    *a = *b;
    *b = tmp;
}

void heapify_down(MinHeap *heap, unsigned int idx) {
    unsigned int menor = idx;
    unsigned int izq = 2 * idx + 1;
    unsigned int der = 2 * idx + 2;

    if (izq < heap->tamano && es_menor(heap->datos[izq], heap->datos[menor]))
        menor = izq;
    if (der < heap->tamano && es_menor(heap->datos[der], heap->datos[menor]))
        menor = der;

    if (menor != idx) {
        swap_nodos(&heap->datos[idx], &heap->datos[menor]);
        heapify_down(heap, menor);
    }
}

void heap_push(MinHeap *heap, Nodo *nodo) {
    unsigned int i = heap->tamano++;
    heap->datos[i] = nodo;
    while (i > 0 && es_menor(heap->datos[i], heap->datos[(i - 1) / 2])) {
        swap_nodos(&heap->datos[i], &heap->datos[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

Nodo *heap_pop(MinHeap *heap) {
    Nodo *raiz = heap->datos[0];
    heap->datos[0] = heap->datos[--heap->tamano];
    heapify_down(heap, 0);
    return raiz;
}

Nodo *construir_arbol_huffman(const unsigned int freq[MAX_CHARS]) {
    MinHeap heap = {.tamano = 0};
    for (int i = 0; i < MAX_CHARS; i++) {
        if (freq[i] > 0) {
            heap_push(&heap, crear_nodo((unsigned char)i, freq[i], 1, NULL, NULL));
        }
    }

    while (heap.tamano > 1) {
        Nodo *izq = heap_pop(&heap);
        Nodo *der = heap_pop(&heap);
        Nodo *padre = crear_nodo(0, izq->frecuencia + der->frecuencia, 0, izq, der);
        heap_push(&heap, padre);
    }
    return heap_pop(&heap);
}

/* --- Generación de códigos --- */

void generar_codigos(Nodo *raiz, char codigos[MAX_CHARS][MAX_TREE_HT],
                     char buffer[], int profundidad) {
    if (raiz->es_hoja) {
        buffer[profundidad] = '\0';
        strcpy(codigos[raiz->caracter], (profundidad > 0) ? buffer : "0");
        return;
    }
    buffer[profundidad] = '0';
    generar_codigos(raiz->izq, codigos, buffer, profundidad + 1);

    buffer[profundidad] = '1';
    generar_codigos(raiz->der, codigos, buffer, profundidad + 1);
}

/* --- Manejo de flujo de bits (BitStream) --- */

void bs_init(BitStream *bs, size_t capacidad_bytes) {
    bs->buffer = (uint8_t *)calloc(capacidad_bytes, sizeof(uint8_t));
    bs->bit_pos = 0;
    bs->capacidad_bytes = capacidad_bytes;
}

void bs_escribir_bit(BitStream *bs, int bit) {
    size_t byte_idx = bs->bit_pos / 8;
    int bit_offset = 7 - (bs->bit_pos % 8);
    if (bit) {
        bs->buffer[byte_idx] |= (1U << bit_offset);
    }
    bs->bit_pos++;
}

void bs_escribir_byte(BitStream *bs, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        bs_escribir_bit(bs, (byte >> i) & 1);
    }
}

int bs_leer_bit(const BitStream *bs, size_t *cursor) {
    size_t byte_idx = (*cursor) / 8;
    int bit_offset = 7 - ((*cursor) % 8);
    (*cursor)++;
    return (bs->buffer[byte_idx] >> bit_offset) & 1;
}

uint8_t bs_leer_byte(const BitStream *bs, size_t *cursor) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (byte << 1) | bs_leer_bit(bs, cursor);
    }
    return byte;
}

/* --- Serialización y Deserialización en Preorden (Opción B) --- */

void serializar_preorden(Nodo *raiz, BitStream *bs) {
    if (raiz->es_hoja) {
        bs_escribir_bit(bs, 1);
        bs_escribir_byte(bs, raiz->caracter);
        return;
    }
    bs_escribir_bit(bs, 0);
    serializar_preorden(raiz->izq, bs);
    serializar_preorden(raiz->der, bs);
}

Nodo *deserializar_preorden(const BitStream *bs, size_t *cursor) {
    int bit = bs_leer_bit(bs, cursor);
    if (bit == 1) {
        uint8_t car = bs_leer_byte(bs, cursor);
        return crear_nodo(car, 0, 1, NULL, NULL);
    }
    Nodo *izq = deserializar_preorden(bs, cursor);
    Nodo *der = deserializar_preorden(bs, cursor);
    return crear_nodo(0, 0, 0, izq, der);
}

/* --- Programa Principal --- */

int main(void) {
    const char *M = "ABRACADABRA_ALGORITMO_AVANZADO_ESCOM";
    size_t N = strlen(M);

    unsigned int freq[MAX_CHARS] = {0};
    for (size_t i = 0; i < N; i++) {
        freq[(unsigned char)M[i]]++;
    }

    /* 1. Entropía empírica de Shannon */
    double entropia = 0.0;
    for (int i = 0; i < MAX_CHARS; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / N;
            entropia -= p * log2(p);
        }
    }

    /* 2. Árbol y códigos de Huffman */
    Nodo *raiz = construir_arbol_huffman(freq);
    char codigos[MAX_CHARS][MAX_TREE_HT] = {{0}};
    char temp[MAX_TREE_HT];
    generar_codigos(raiz, codigos, temp, 0);

    /* 3. Longitud promedio L */
    double L = 0.0;
    printf("Tabla de frecuencias y codigos:\n");
    for (int i = 0; i < MAX_CHARS; i++) {
        if (freq[i] > 0) {
            size_t len = strlen(codigos[i]);
            L += ((double)freq[i] / N) * len;
            printf("  '%c' | f = %2u | p = %6.4f | codigo = %-6s (|c| = %zu)\n",
                   (char)i, freq[i], (double)freq[i] / N, codigos[i], len);
        }
    }

    printf("\nEntropia H(X):        %.4f bits/simbolo\n", entropia);
    printf("Longitud media L:     %.4f bits/simbolo\n", L);
    printf("Desigualdad Shannon:  %.4f <= %.4f < %.4f (%s)\n",
           entropia, L, entropia + 1.0,
           (entropia <= L && L < entropia + 1.0) ? "Se cumple" : "No se cumple");

    /* 4. Serialización (Opción B) y Compresión en memoria */
    BitStream bs;
    bs_init(&bs, 1024);

    serializar_preorden(raiz, &bs);
    size_t bits_encabezado = bs.bit_pos;
    size_t bytes_encabezado = (bits_encabezado + 7) / 8;

    for (size_t i = 0; i < N; i++) {
        const char *cod = codigos[(unsigned char)M[i]];
        for (size_t j = 0; cod[j] != '\0'; j++) {
            bs_escribir_bit(&bs, cod[j] - '0');
        }
    }

    size_t bits_totales = bs.bit_pos;
    size_t bytes_totales = (bits_totales + 7) / 8;

    printf("\nSobrecarga Opcion B:  %zu bits (%zu bytes)\n",
           bits_encabezado, bytes_encabezado);
    printf("Datos comprimidos:    %zu bits (%zu bytes total vs %zu bytes ASCII)\n",
           bits_totales - bits_encabezado, bytes_totales, N);

    /* 5. Descompresión y verificación */
    size_t cursor = 0;
    Nodo *raiz_reconstruida = deserializar_preorden(&bs, &cursor);
    char *recuperado = (char *)malloc(N + 1);

    for (size_t i = 0; i < N; i++) {
        Nodo *actual = raiz_reconstruida;
        while (!actual->es_hoja) {
            int bit = bs_leer_bit(&bs, &cursor);
            actual = (bit == 0) ? actual->izq : actual->der;
        }
        recuperado[i] = (char)actual->caracter;
    }
    recuperado[N] = '\0';

    printf("Mensaje recuperado:   %s\n", recuperado);
    printf("Verificacion:         %s\n",
           strcmp(M, recuperado) == 0 ? "EXACTA" : "ERROR");

    free(recuperado);
    free(bs.buffer);
    liberar_arbol(raiz);
    liberar_arbol(raiz_reconstruida);
    return 0;
}
