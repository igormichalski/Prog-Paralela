#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <mpi.h>

typedef struct {
    int origem;
    int destino;
    double peso;
} Aresta;

typedef struct {
    double peso;
    int origem;
    int destino;
} ArestaMin;

long long tamanho_chunk(long long j, long long num_chunks, long long chunk, long long M) {
    if (j < num_chunks - 1) {
        return chunk; 
    }
    return M - (num_chunks - 1) * chunk; //ultimo chunk
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
    if (ra == rb) {
        return;
    }
    if (altura[ra] < altura[rb]) {
        componente[ra] = rb;
    } else if (altura[ra] > altura[rb]) {
        componente[rb] = ra;
    } else {
        componente[rb] = ra;
        altura[ra]++; 
    }
}

//for-preferido 
void atualizar_menor(ArestaMin *menor, int comp, int origem, int destino, double peso) {
    int candidata_ganha = 0;

    if (peso < menor[comp].peso){ //peso menor ganha
        candidata_ganha = 1;             
    } else if (peso == menor[comp].peso){ //empate no peso
        if (origem < menor[comp].origem){ //origem menor ganh
            candidata_ganha = 1;                     
        } else if (origem == menor[comp].origem) { //empate dnv so que na origem
            if (destino < menor[comp].destino){ //destino menor ganha
                candidata_ganha = 1;  
            }
        }
    }

    if (candidata_ganha) {
        menor[comp].peso    = peso;
        menor[comp].origem  = origem;
        menor[comp].destino = destino;
    }
}

ArestaMin *garantir_capacidade(ArestaMin *menor, long long *capacidade, long long necessaria) {
    if (necessaria <= *capacidade){
        return menor;
    }

    long long nova;
    if (*capacidade == 0){
        nova = 1024;
    }else{
        nova = *capacidade;
    }

    while (nova < necessaria){
        nova = nova * 2;
    }
    //Realoca o vetor menor (a ideia de ir dobrando usado acima foi para tentar diminuir a quantidade de realloc)
    ArestaMin *novo = realloc(menor, (size_t)nova * sizeof(ArestaMin));
    if (novo == NULL) {
        printf("Erro: memoria insuficiente ao crescer o vetor de menores\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    //somente atualiza dos novos componentes ao fim, nao mexe no que ja tinha no vetor
    for (long long i = *capacidade; i < nova; i++) {
        novo[i].peso = DBL_MAX; 
    }
    *capacidade = nova;
    return novo;
}

void dobrar_rodada1(Aresta *arr, long long ini, long long fim, ArestaMin **menor, long long *capacidade, int *meu_maior) {
    for (long long i = ini; i < fim; i++) {
        int u = arr[i].origem, v = arr[i].destino;
        double w = arr[i].peso;
        
        //Ataualiza o maior global que vai ser usado para alocar a componente e altura
        if(u > *meu_maior) {
            *meu_maior = u;
        }
        if (v > *meu_maior){
            *meu_maior = v;
        }
        if(u == v){
            continue;                            
        }
       
        //Pega o maior dos dois vertices para chamar o garantir capacidade dps
        long long maior;
        if (u > v) {
            maior = u;
        } else {
            maior = v;
        }
       
        *menor = garantir_capacidade(*menor, capacidade, maior + 1);
        //atualizar_menor(menor[], (qual componente), dados da aresta1, dados da aresta2, dados da aresta3)
        atualizar_menor(*menor, u, u, v, w);  
        atualizar_menor(*menor, v, u, v, w); 
    }
}

//regra para unir menor local com menor global
void op_menor_aresta(void *in, void *inout, int *len, MPI_Datatype *tipo) {
    (void)tipo;
    ArestaMin *a = (ArestaMin *)in; // candidata que chegou
    ArestaMin *b = (ArestaMin *)inout; // melhor ate agora

    for (int i = 0; i < *len; i++) {
        if (a[i].peso == DBL_MAX) {
            continue;                    
        }

        int candidata_vence = 0;
        //mesmo criteiro que atualizar_menor()
        if (b[i].peso == DBL_MAX) {
            candidata_vence = 1;    
        } else if(a[i].peso < b[i].peso){
            candidata_vence = 1;                      
        } else if (a[i].peso == b[i].peso){          
            if (a[i].origem < b[i].origem) {
                candidata_vence = 1;                
            } else if (a[i].origem == b[i].origem) {  
                if (a[i].destino < b[i].destino) {
                    candidata_vence = 1;              
                }
            }
        }

        if (candidata_vence) {
            b[i] = a[i];
        }
    }
}

int main(int argc, char *argv[]) {

    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    char maquina[MPI_MAX_PROCESSOR_NAME];
    int len_maquina;
    MPI_Get_processor_name(maquina, &len_maquina);

    if (argc < 2) {
        if(rank == 0){
            printf("Uso: mpirun -np <n> %s <arquivo_binario> [tempo_sequencial]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Datatype MPI_ARESTA;
    MPI_Type_contiguous(sizeof(Aresta), MPI_BYTE, &MPI_ARESTA);
    MPI_Type_commit(&MPI_ARESTA);
    
    size_t arestas_por_chunk = (16 * 1024 * 1024) / sizeof(Aresta);

    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio = MPI_Wtime();

    ArestaMin *menor_local = NULL; 
    long long capacidade = 0; //posicoes alocadas em menor_local
    int meu_maior = 0;   //maior id de vertice visto na minha fatia
    long long M = 0; //numero total de arestas
    FILE *arq = NULL;

    if(rank == 0){
        arq = fopen(argv[1], "rb");
        if (arq == NULL) {
            printf("Erro: nao consegui abrir o arquivo '%s'\n", argv[1]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (fseeko(arq, 0, SEEK_END) != 0){
            printf("Erro ao posicionar no fim do arquivo\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        long long tamanho = ftello(arq);
        rewind(arq);                  
        if (tamanho % (long long)sizeof(Aresta) != 0){
            printf("Tamanho %lld nao e multiplo de %zu.\n", tamanho, sizeof(Aresta));
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        M = tamanho / (long long)sizeof(Aresta);
    }

    //envia M para geral
    MPI_Bcast(&M, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);


    //Pre-Calculo que o RR precisa
    long long chunk = (long long)arestas_por_chunk; //arestas por chunk de envio
    long long num_chunks = (M + chunk - 1) / chunk;  

    //Conta quantos chunks cada um vai receber para poder depois alocar fixo e nao dinamico(atacando realloc dnv)
    long long *contagens = malloc((size_t)nprocs * sizeof(long long));
    for (int p = 0; p < nprocs; p++) {
        contagens[p] = 0;
    }
    for (long long j = 0; j < num_chunks; j++) { //
        long long sz = tamanho_chunk(j, num_chunks, chunk, M);
        contagens[(int)(j % nprocs)] += sz;
    }
    long long minhas = contagens[rank]; //tamanho da minha fatia


    //aloca o tamanho necessario para armazenar todas as arestas que vamos receber por cada processador
    Aresta *minhas_arestas = NULL;
    if (minhas > 0){
        minhas_arestas = malloc((size_t)minhas * sizeof(Aresta));
        if (minhas_arestas == NULL){
            printf("[rank %d] Erro: memoria insuficiente para %lld arestas\n", rank, minhas);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    if (rank == 0) {
        //aloca 2 buffer para fazer enviar um e ler no outro
        Aresta *buf[2];
        buf[0] = malloc((size_t)chunk * sizeof(Aresta));
        buf[1] = malloc((size_t)chunk * sizeof(Aresta));
        if (buf[0] == NULL || buf[1] == NULL) {
            printf("Erro: memoria insuficiente para os buffers de chunk\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        MPI_Request req[2] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};
        int alt = 0; 
        long long meu_off = 0;        

        for (long long j = 0; j < num_chunks; j++) {
            long long este = tamanho_chunk(j, num_chunks, chunk, M); //este = quantas arestas neste bloco
            int dest = (int)(j % nprocs);                          

            MPI_Wait(&req[alt], MPI_STATUS_IGNORE);

            size_t lidas = fread(buf[alt], sizeof(Aresta), (size_t)este, arq);

            if (lidas != (size_t)este){
                printf("Erro: leitura incompleta (esperado %lld, lido %zu)\n", este, lidas);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            if (dest == 0){
                memcpy(minhas_arestas + meu_off, buf[alt], (size_t)este * sizeof(Aresta)); //minhas+ meuoffset para nao sobrescrever o que ja temos
                dobrar_rodada1(minhas_arestas, meu_off, meu_off + este, &menor_local, &capacidade, &meu_maior);
                meu_off += este;
                req[alt] = MPI_REQUEST_NULL; 
            }else{
                MPI_Isend(buf[alt], (int)este, MPI_ARESTA, dest, 0, MPI_COMM_WORLD, &req[alt]);
            }
            //alterna entre os buffers 0 e 1
            if (alt == 0) {
                alt = 1;
            }else{
                alt = 0;
            } 
        }

        //espera o ultimo chegar
        MPI_Wait(&req[0], MPI_STATUS_IGNORE);   
        MPI_Wait(&req[1], MPI_STATUS_IGNORE);
        free(buf[0]);
        free(buf[1]);
        fclose(arq);
    }else if (minhas > 0){
        long long off = 0;  
        long long j = rank; 
        long long cur_sz = tamanho_chunk(j, num_chunks, chunk, M);
        MPI_Request req;

        MPI_Irecv(minhas_arestas + off, (int)cur_sz, MPI_ARESTA, 0, 0, MPI_COMM_WORLD, &req);

        while (off < minhas){
            MPI_Wait(&req, MPI_STATUS_IGNORE);       

            long long prox_j = j + nprocs;    
            long long prox_off = off + cur_sz;
            long long prox_sz = 0;
            if (prox_j < num_chunks) {               
                prox_sz = tamanho_chunk(prox_j, num_chunks, chunk, M);
                MPI_Irecv(minhas_arestas + prox_off, (int)prox_sz, MPI_ARESTA, 0, 0, MPI_COMM_WORLD, &req);
            }
            dobrar_rodada1(minhas_arestas, off, off + cur_sz, &menor_local, &capacidade, &meu_maior);

            off = prox_off;
            j = prox_j;
            cur_sz = prox_sz;
        }
    }

    int maior_global = 0;
    MPI_Allreduce(&meu_maior, &maior_global, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    long long N = (long long)maior_global + 1; //numero de vertices
    
    printf("[rank %d em %s] fatia = %lld arestas\n", rank, maquina, minhas);

    menor_local = garantir_capacidade(menor_local, &capacidade, N);

    int *componente = malloc((size_t)N * sizeof(int));
    int *altura = calloc((size_t)N, sizeof(int)); 
    if (componente == NULL || altura == NULL) {
        printf("[rank %d] Erro: memoria insuficiente para a floresta F\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (long long v = 0; v < N; v++) {
        componente[v] = (int)v;
    }

    ArestaMin *menor_global = malloc((size_t)N * sizeof(ArestaMin));
    int *indice_componente = malloc((size_t)N * sizeof(int));
    if (menor_global == NULL || indice_componente == NULL) {
        printf("[rank %d] Erro: memoria insuficiente para os buffers do reduce\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Datatype MPI_ARESTAMIN; 
    MPI_Type_contiguous(sizeof(ArestaMin), MPI_BYTE, &MPI_ARESTAMIN);
    MPI_Type_commit(&MPI_ARESTAMIN);

    MPI_Op MPI_MENOR_ARESTA;
    MPI_Op_create(op_menor_aresta, 1, &MPI_MENOR_ARESTA);

    Aresta   *arvore     = NULL;
    long long n_arvore   = 0;
    double    peso_total = 0.0;
    double   *peso_comp  = NULL;   //soma parcial da MST acumulada dentro de cada componente (so rank 0)
    if (rank == 0) {
        arvore = malloc((size_t)N * sizeof(Aresta));
        if (arvore == NULL) {
            printf("Erro: memoria insuficiente para a arvore (rank 0)\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        peso_comp = calloc((size_t)N, sizeof(double));
        if (peso_comp == NULL) {
            printf("Erro: memoria insuficiente para peso_comp (rank 0)\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    int n_rod = 0;       
    int primeira_rodada = 1;       
    int finalizado = 0;
    while (!finalizado) {
        int nroots = 0;
        for (long long i = 0; i < N; i++) {
            if (componente[i] == (int)i) {
                indice_componente[i] = nroots; //mapa de raiz ativas 
                nroots++;
            }
        }

        if(primeira_rodada){
            primeira_rodada = 0;
        } else {
            for (long long v = 0; v < N; v++) {
                int raiz = (int)v;
                while (componente[raiz] != raiz) {
                    raiz = componente[raiz];
                }
                componente[v] = raiz; 
            }

            for (int c = 0; c < nroots; c++) {
                menor_local[c].peso = DBL_MAX;
            }
            long long wpos = 0; //cursor de escrita da poda

            for (long long i = 0; i < minhas; i++) {
                int u = minhas_arestas[i].origem;
                int v = minhas_arestas[i].destino;
                double w = minhas_arestas[i].peso;

                int ru = componente[u];
                int rv = componente[v];

                if (ru == rv){
                    continue; //interna entao poda e sai do vetor
                }

                if (wpos != i) {
                    minhas_arestas[wpos] = minhas_arestas[i];
                }
                wpos++;

                int cu = indice_componente[ru];
                int cv = indice_componente[rv];
                atualizar_menor(menor_local, cu, u, v, w);
                atualizar_menor(menor_local, cv, u, v, w);
            }
            minhas = wpos;
        }

        MPI_Allreduce(menor_local, menor_global, nroots, MPI_ARESTAMIN, MPI_MENOR_ARESTA, MPI_COMM_WORLD);

        int progrediu = 0;
        for (int c = 0; c < nroots; c++){
            if (menor_global[c].peso == DBL_MAX){
                continue;                                
            }
            int u = menor_global[c].origem;
            int v = menor_global[c].destino;

            int ru = achar_componente(componente, u);
            int rv = achar_componente(componente, v);
            if (ru == rv){
                continue;
            }
            juntar(componente, altura, u, v);
            if (rank == 0) {
                double soma_galho = peso_comp[ru] + peso_comp[rv] + menor_global[c].peso;
                int nova_raiz = achar_componente(componente, u); //raiz que sobrou depois da fusao
                peso_comp[nova_raiz] = soma_galho;

                arvore[n_arvore].origem  = u;
                arvore[n_arvore].destino = v;
                arvore[n_arvore].peso = menor_global[c].peso;
                n_arvore++;
            }
            progrediu = 1;
        }
        if (!progrediu) {
            finalizado = 1;  
        }
        n_rod++;
    }

    double meu_tempo = MPI_Wtime() - t_inicio;
    double tempo;
    MPI_Reduce(&meu_tempo, &tempo, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        peso_total = peso_comp[achar_componente(componente, 0)];

        FILE *saida = fopen("arvore_mst_paralelo.txt", "w");
        if (saida == NULL) {
            printf("Erro: arquivo de saida nao abriu para escrita\n");
        } else {
            for (long long e = 0; e < n_arvore; e++) {
                fprintf(saida, "%d %.12f %d\n", arvore[e].origem, arvore[e].peso, arvore[e].destino);
            }
            fclose(saida);
        }
    }

    free(minhas_arestas);
    free(contagens);
    free(menor_local);
    free(componente);
    free(altura);
    free(menor_global);
    free(indice_componente);
    if (rank == 0) {
        free(arvore);
        free(peso_comp);
    }


    if (rank == 0){
        double t_seq = atof(argv[2]);
        double eficiencia = (t_seq / tempo) / nprocs;
        
        printf("\nResultado: peso = %.12f\n", peso_total);
        printf("Eficiencia= %.2f%%\n", 100.0 * eficiencia);
        printf("Tempo= %.6f s\n", tempo);
    }

    MPI_Op_free(&MPI_MENOR_ARESTA);
    MPI_Type_free(&MPI_ARESTAMIN);
    MPI_Type_free(&MPI_ARESTA);
    MPI_Finalize();
    return 0;
}