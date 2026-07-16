#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    int origem;
    int destino;
    double peso;
} Aresta; //16bytes

typedef struct {
    int existe;
    int origem;
    int destino;
    double peso;
} ArestaMin;

double agora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}

int achar_componente(int *componente, int x) {
    while (componente[x] != x) {
        componente[x] = componente[componente[x]];
        x = componente[x];
    }
    return x;
}

void juntar(int *componente, int *altura, int a, int b) {
    int ra = achar_componente(componente, a);
    int rb = achar_componente(componente, b);
    if (ra == rb) return;                 
    if (altura[ra] < altura[rb]) {
        componente[ra] = rb;
    } else if (altura[ra] > altura[rb]) {
        componente[rb] = ra;
    } else {
        componente[rb] = ra;
        altura[ra]++;                     
    }
}

int for_preferido_sobre(Aresta candidata, ArestaMin atual) {
    if (!atual.existe) return 1;
    if (candidata.peso < atual.peso) return 1;
    if (candidata.peso > atual.peso) return 0;
    if (candidata.origem < atual.origem) return 1;
    if (candidata.origem > atual.origem) return 0;
    if (candidata.destino < atual.destino) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    
    if (argc < 2) {
        printf("Uso: %s <arquivo_binario>\n", argv[0]);
        return 1;
    }

    printf("Ok..\n");
    //Inicia o contador
    double t_inicio = agora();

    FILE *arq = fopen(argv[1], "rb");
    if (arq == NULL) {
        printf("Erro: nao consegui abrir o arquivo '%s'\n", argv[1]);
        return 1;
    }

    //Pega o tamanho total do arquivo
    if (fseeko(arq, 0, SEEK_END) != 0) { 
        printf("Erro ao posicionar no fim do arquivo\n");
        fclose(arq);
        return 1;
    }
    long long tamanho = ftello(arq); //Retorna o numero do byte que o ponteiro esta
    rewind(arq); 

    long long bytes_por_aresta = sizeof(Aresta); 
    if (tamanho % bytes_por_aresta != 0) {
        printf("Erro: tamanho (%lld bytes) nao e multiplo de %lld.\n", tamanho, bytes_por_aresta);
        fclose(arq);
        return 1;
    }

    //Blocos com 16 MB
    size_t arestas_por_bloco = (16 * 1024 * 1024) / sizeof(Aresta);
    Aresta *bloco = malloc(arestas_por_bloco * sizeof(Aresta)); //1milhao por bloco
    if (bloco == NULL) {
        printf("Erro: memoria insuficiente para o buffer de leitura\n");
        fclose(arq);
        return 1;
    }

    int maior_id = 0;
    size_t lidas;
    while ((lidas = fread(bloco, sizeof(Aresta), arestas_por_bloco, arq)) > 0) {
        for (size_t i = 0; i < lidas; i++) {
            if (bloco[i].origem  > maior_id) maior_id = bloco[i].origem;
            if (bloco[i].destino > maior_id) maior_id = bloco[i].destino;
        }
    }

    long long num_vertices = (long long)maior_id + 1;

    //Tudo que precisamos para o laco principal
    int *componente  = malloc((size_t)num_vertices * sizeof(int));
    int *altura      = calloc((size_t)num_vertices, sizeof(int));         
    ArestaMin *menor = malloc((size_t)num_vertices * sizeof(ArestaMin));  
    
    if (componente == NULL || altura == NULL || menor == NULL) {
        printf("Erro: memoria insuficiente para a floresta F\n");
        return 1;
    }
    for (int v = 0; v < num_vertices; v++) { //Inicializa as componentes
        componente[v] = v;
    }

    Aresta *arvore = malloc((size_t)num_vertices * sizeof(Aresta));
    if (arvore == NULL) {
        printf("Erro: memoria insuficiente para a arvore\n");
        return 1;
    }
    long long n_arvore = 0;
    double peso_total = 0.0;

    //soma parcial da MST acumulada dentro de cada componente
    double *peso_comp = calloc((size_t)num_vertices, sizeof(double));
    if (peso_comp == NULL) {
        printf("Erro: memoria insuficiente para peso_comp\n");
        return 1;
    }

    //Inicio laco principal
    int concluido = 0;
    while (!concluido) {
        for (int i = 0; i < num_vertices; i++) {
            menor[i].existe = 0;
        }

        //Passa por todo o arquivo e atualiza a menor aresta dos componentes de u e de v usando for-preferido-sobre.
        rewind(arq);
        while ((lidas = fread(bloco, sizeof(Aresta), arestas_por_bloco, arq)) > 0) {
            for (size_t i = 0; i < lidas; i++) {
                int cu = achar_componente(componente, bloco[i].origem);
                int cv = achar_componente(componente, bloco[i].destino);
                if (cu == cv) continue;   // mesma componente: ignora a aresta
                if (for_preferido_sobre(bloco[i], menor[cu])) {
                    menor[cu].existe  = 1;
                    menor[cu].origem  = bloco[i].origem;
                    menor[cu].destino = bloco[i].destino;
                    menor[cu].peso    = bloco[i].peso;
                }
                if (for_preferido_sobre(bloco[i], menor[cv])) {
                    menor[cv].existe  = 1;
                    menor[cv].origem  = bloco[i].origem;
                    menor[cv].destino = bloco[i].destino;
                    menor[cv].peso    = bloco[i].peso;
                }
            }
        }

        //Verifica se o loop conseguiu encontrar uma aresta minima, se nao achou significa que o grafo é uma unica componente somente
        int todos_nenhuma = 1;
        for (int i = 0; i < num_vertices; i++) {
            if (menor[i].existe) {
                todos_nenhuma = 0;
                break;
            }
        }

        if (todos_nenhuma) {
            concluido = 1;
        } else {
            concluido = 0;
            for (int i = 0; i < num_vertices; i++) {
                if (!menor[i].existe) continue; //Pulas as posicoes sem arestas

                //pego as raizes ANTES de juntar, pois preciso das duas somas parciais
                int ru = achar_componente(componente, menor[i].origem);
                int rv = achar_componente(componente, menor[i].destino);
                if (ru == rv) continue; //Pula se tiver na mesma componente

                juntar(componente, altura, menor[i].origem, menor[i].destino);

                double soma_galho = peso_comp[ru] + peso_comp[rv] + menor[i].peso;
                int nova_raiz = achar_componente(componente, menor[i].origem); 
                peso_comp[nova_raiz] = soma_galho;

                //salva as arestas de menor para formar o MST no fim
                arvore[n_arvore].origem  = menor[i].origem;
                arvore[n_arvore].destino = menor[i].destino;
                arvore[n_arvore].peso    = menor[i].peso;
                n_arvore++;
            }
        }
    }
    //Fim laco principal

    //Finaliza o contador
    double tempo = agora() - t_inicio;

    peso_total = peso_comp[achar_componente(componente, 0)];

    fclose(arq);
    free(bloco);
    free(componente);
    free(altura);
    free(menor);
    free(peso_comp);

    printf("Distancia da arvore geradora minima: %.12f\n", peso_total);
    printf("Tempo de execucao: %.6f segundos\n", tempo);

    FILE *saida = fopen("arvore_mst_sequencial.txt", "w");
    if (saida == NULL) {
        printf("Erro: arquivo de saida nao abriu para escrita");
        free(arvore);
        return 1;
    }
    for (long long e = 0; e < n_arvore; e++) {
        fprintf(saida, "%d %.12f %d\n", arvore[e].origem, arvore[e].peso, arvore[e].destino);
    }
    fclose(saida);

    free(arvore);
    return 0;
}