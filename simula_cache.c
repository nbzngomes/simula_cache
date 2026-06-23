#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct LinhaCache {
    int rotulo;
    int dirty;
    int lru; 
    struct LinhaCache *prox;
} LinhaCache;

typedef struct {
    LinhaCache *head;
    int num_linhas;
} Conjunto;

typedef struct {
    int politica_escrita;
    int tam_linha;
    int num_linhas_total;
    int assoc;
    double hit_time;
    int politica_subst;
    double mem_leitura;
    double mem_escrita;
    int num_conjuntos;
    int bits_palavra;
    int bits_conjunto;
} ConfigCache;

typedef struct {
    long total_acessos;
    long total_leituras;
    long total_escritas;
    long hits_leitura;
    long hits_escrita;
    long misses_leitura;
    long misses_escrita;
    long escritas_mem_principal;
    long leituras_mem_principal;
    double tempo_total;
} Estatisticas;

LinhaCache *nova_linha(int rotulo) {
    LinhaCache *linha = (LinhaCache *)malloc(sizeof(LinhaCache));
    if (!linha) {
        fprintf(stderr, "Erro: falha ao alocar memoria para linha de cache.\n");
        exit(1);
    }
    linha->rotulo = rotulo;
    linha->dirty = 0;
    linha->lru = 0;
    linha->prox = NULL;
    return linha;
}

LinhaCache *buscar_linha(Conjunto *conj, int rotulo) {
    LinhaCache *atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo == rotulo)
            return atual;
        atual = atual->prox;
    }
    return NULL;
}

void incrementar_lru(Conjunto *conj) {
    LinhaCache *atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo != -1)
            atual->lru++;
        atual = atual->prox;
    }
}

LinhaCache *linha_lru(Conjunto *conj) {
    LinhaCache *lru = NULL;
    LinhaCache *atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo != -1) {
            if (lru == NULL || atual->lru > lru->lru)
                lru = atual;
        }
        atual = atual->prox;
    }
    return lru;
}

LinhaCache *linha_vaga(Conjunto *conj) {
    LinhaCache *atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo == -1)
            return atual;
        atual = atual->prox;
    }
    return NULL;
}

LinhaCache *linha_aleatoria(Conjunto *conj, int assoc) {
    int sorteio = rand() % assoc;
    int i = 0;
    LinhaCache *atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo != -1) {
            if (i == sorteio)
                return atual;
            i++;
        }
        atual = atual->prox;
    }
    atual = conj->head;
    while (atual != NULL) {
        if (atual->rotulo != -1)
            return atual;
        atual = atual->prox;
    }
    return NULL;
}

void inserir_linha(Conjunto *conj, LinhaCache *linha) {
    linha->prox = conj->head;
    conj->head = linha;
    conj->num_linhas++;
}

Conjunto *inicializar_cache(ConfigCache *cfg) {
	int i, j;
    Conjunto *cache = (Conjunto *)malloc(cfg->num_conjuntos * sizeof(Conjunto));
    if (!cache) {
        fprintf(stderr, "Erro: falha ao alocar memoria para os conjuntos.\n");
        exit(1);
    }

    for (i = 0; i < cfg->num_conjuntos; i++) {
        cache[i].head = NULL;
        cache[i].num_linhas = 0;

        for (j = 0; j < cfg->assoc; j++) {
    		inserir_linha(&cache[i], nova_linha(-1));
        }
        cache[i].num_linhas = 0;
    }
    return cache;
}

void limpa_cache(Conjunto *cache, int num_conjuntos) {
	int i;
    for (i = 0; i < num_conjuntos; i++) {
        LinhaCache *atual = cache[i].head;
        while (atual != NULL) {
            LinhaCache *tmp = atual;
            atual = atual->prox;
            free(tmp);
        }
    }
    free(cache);
}

void decompor_endereco(unsigned int endereco, ConfigCache *cfg,
                        int *rotulo, int *indice_conj, int *palavra) {
    *palavra = endereco & (cfg->tam_linha - 1);
    *indice_conj = (endereco >> cfg->bits_palavra) & (cfg->num_conjuntos - 1);
    *rotulo = endereco >> (cfg->bits_palavra + cfg->bits_conjunto);
}

void acessar_cache(Conjunto *cache, ConfigCache *cfg, Estatisticas *stats,
                   unsigned int endereco, char operacao) {
    int rotulo, indice_conj, palavra;
    Conjunto *conj;
    LinhaCache *linha;
    LinhaCache *vaga;
    LinhaCache *subst;
    decompor_endereco(endereco, cfg, &rotulo, &indice_conj, &palavra);

    conj = &cache[indice_conj];
    linha = buscar_linha(conj, rotulo);

    if (linha != NULL) {
        if (operacao == 'R') {
            stats->hits_leitura++;
            stats->tempo_total += cfg->hit_time;
        } else {
            stats->hits_escrita++;
            if (cfg->politica_escrita == 0) {
                stats->escritas_mem_principal++;
                stats->tempo_total += cfg->hit_time + cfg->mem_escrita;
            } else {
                linha->dirty = 1;
                stats->tempo_total += cfg->hit_time;
            }
        }
        incrementar_lru(conj);
        linha->lru = 0;
        return;
    }

    if (operacao == 'R') {
        stats->misses_leitura++;
        stats->leituras_mem_principal++;
        stats->tempo_total += cfg->hit_time + cfg->mem_leitura;
    } else {
        stats->misses_escrita++;
        if (cfg->politica_escrita == 0) {
            stats->escritas_mem_principal++;
            stats->tempo_total += cfg->mem_escrita;
            return;
        } else {
            stats->leituras_mem_principal++;
            stats->tempo_total += cfg->hit_time + cfg->mem_leitura;
        }
    }

    vaga = linha_vaga(conj);

    if (vaga != NULL) {
        vaga->rotulo = rotulo;
        vaga->dirty = (operacao == 'W' && cfg->politica_escrita == 1) ? 1 : 0;
        incrementar_lru(conj);
        vaga->lru = 0;
        conj->num_linhas++;
    } else {
        subst = NULL;
        if (cfg->politica_subst == 0)
            subst = linha_lru(conj);
        else
            subst = linha_aleatoria(conj, cfg->assoc);

        if (cfg->politica_escrita == 1 && subst->dirty) {
            stats->escritas_mem_principal++;
            stats->tempo_total += cfg->mem_escrita;
        }

        subst->rotulo = rotulo;
        subst->dirty = (operacao == 'W' && cfg->politica_escrita == 1) ? 1 : 0;
        incrementar_lru(conj);
        subst->lru = 0;
    }
}

void flush_cache(Conjunto *cache, ConfigCache *cfg, Estatisticas *stats) {
	int i;
	
    if (cfg->politica_escrita != 1) return;
    
    for (i = 0; i < cfg->num_conjuntos; i++) {
        LinhaCache *atual = cache[i].head;
        while (atual != NULL) {
            if (atual->rotulo != -1 && atual->dirty) {
                stats->escritas_mem_principal++;
            }
            atual = atual->prox;
        }
    }
}

void imprimir_resultados(ConfigCache *cfg, Estatisticas *stats) {
    long total_hits = stats->hits_leitura + stats->hits_escrita;
    long total_misses = stats->misses_leitura + stats->misses_escrita;
    long total = total_hits + total_misses;

    double hit_rate_global = (total > 0) ? (double)total_hits / total : 0.0;
    double hit_rate_leitura = (stats->total_leituras > 0) ? (double)stats->hits_leitura / stats->total_leituras : 0.0;
    double hit_rate_escrita = (stats->total_escritas > 0) ? (double)stats->hits_escrita / stats->total_escritas : 0.0;

    double miss_rate = 1.0 - hit_rate_global;
    double penalidade = cfg->mem_leitura; 
    double amat = cfg->hit_time+ miss_rate * penalidade;

    printf("=============================================\n");
    printf("           	RESULTADOS            			\n");
    printf("=============================================\n\n");

    printf("Politica de escrita   : %s\n", cfg->politica_escrita == 0 ? "Write-through" : "Write-back");
    printf("Tamanho da linha      : %d bytes\n", cfg->tam_linha);
    printf("Numero de linhas      : %d\n", cfg->num_linhas_total);
    printf("Associatividade       : %d vias\n", cfg->assoc);
    printf("Numero de conjuntos   : %d\n", cfg->num_conjuntos);
    printf("Hit time              : %.4f ns\n", cfg->hit_time);
    printf("Politica de subst.    : %s\n", cfg->politica_subst == 0 ? "LRU" : "Aleatoria");
    printf("Tempo leitura	      : %.4f ns\n", cfg->mem_leitura);
    printf("Tempo escrita	      : %.4f ns\n", cfg->mem_escrita);
    printf("Bits de palavra       : %d\n", cfg->bits_palavra);
    printf("Bits de conjunto      : %d\n", cfg->bits_conjunto);
    printf("Bits de rotulo        : %d\n", 32 - (cfg->bits_conjunto + cfg->bits_palavra));

    printf("\n--- Enderecos no arquivo de entrada ---\n");
    printf("Total de leituras (R) : %ld\n", stats->total_leituras);
    printf("Total de escritas (W) : %ld\n", stats->total_escritas);
    printf("Total de acessos      : %ld\n", stats->total_acessos);

    printf("\n--- Acessos a memoria principal ---\n");
    printf("Leituras              : %ld\n", stats->leituras_mem_principal);
    printf("Escritas              : %ld\n", stats->escritas_mem_principal);

    printf("\n--- Hitrate ---\n");
    printf("Leitura               : %.4f  (%ld/%ld)\n",
           hit_rate_leitura, stats->hits_leitura, stats->total_leituras);
    printf("Escrita               : %.4f  (%ld/%ld)\n",
           hit_rate_escrita, stats->hits_escrita, stats->total_escritas);
    printf("Global                : %.4f  (%ld/%ld)\n",
           hit_rate_global, total_hits, total);

    printf("\n--- Tempo ---\n");
    printf("Tempo medio de acesso 	    : %.4f ns\n", amat);
    printf("Tempo total simulado        : %.4f ns\n", stats->tempo_total);
    printf("=======================================================\n");
}

int main(int argc, char *argv[]) {
	ConfigCache cfg;
    Estatisticas stats;
    Conjunto *cache;
    FILE *fp;
    char linha_buf[64];
    char *arquivo;
    
    if (argc < 9) {
        fprintf(stderr,
            "Uso: %s <politica_escrita> <tam_linha> <num_linhas> <assoc>"
            " <hit_time> <LRU|RAND> <mem_tempo>\n"
            "Exemplo: %s 0 64 4096 2 10 LRU 80\n",
            argv[0], argv[0]);
        return 1;
    }

    srand((unsigned int)time(NULL));

    cfg.politica_escrita = atoi(argv[2]);
    cfg.tam_linha = atoi(argv[3]);
    cfg.num_linhas_total = atoi(argv[4]);
    cfg.assoc = atoi(argv[5]);
    cfg.hit_time = atof(argv[6]);
    cfg.politica_subst = (strcmp(argv[7], "LRU") == 0) ? 0 : 1;
    cfg.mem_leitura = atof(argv[8]);
    cfg.mem_escrita = atof(argv[9]);
    arquivo = "oficial.cache";

    if (cfg.politica_escrita != 0 && cfg.politica_escrita != 1) {
        fprintf(stderr, "Erro: politica de escrita deve ser 0 ou 1.\n");
        return 1;
    }
    if (cfg.assoc < 1 || cfg.assoc > cfg.num_linhas_total) {
        fprintf(stderr, "Erro: associatividade fora do intervalo valido.\n");
        return 1;
    }
    if (cfg.num_linhas_total % cfg.assoc != 0) {
        fprintf(stderr, "Erro: numero de linhas deve ser divisivel pela associatividade.\n");
        return 1;
    }

    cfg.num_conjuntos = cfg.num_linhas_total / cfg.assoc;
    cfg.bits_palavra = (int)log2((double)cfg.tam_linha);
    cfg.bits_conjunto = (int)log2((double)cfg.num_conjuntos);

    cache = inicializar_cache(&cfg);
    memset(&stats, 0, sizeof(Estatisticas));

    fp = fopen(arquivo, "r");
    if (!fp) {
        fprintf(stderr, "Erro: nao foi possivel abrir o arquivo '%s'.\n", arquivo);
        limpa_cache(cache, cfg.num_conjuntos);
        return 1;
    }

    while (fgets(linha_buf, sizeof(linha_buf), fp)) {
        unsigned int endereco;
        char op;
        if (sscanf(linha_buf, "%x %c", &endereco, &op) != 2)
            continue;

        stats.total_acessos++;
        if (op == 'R')
            stats.total_leituras++;
        else if (op == 'W')
            stats.total_escritas++;

        acessar_cache(cache, &cfg, &stats, endereco, op);
    }
    fclose(fp);

    flush_cache(cache, &cfg, &stats);

    imprimir_resultados(&cfg, &stats);

    limpa_cache(cache, cfg.num_conjuntos);
    return 0;
}
