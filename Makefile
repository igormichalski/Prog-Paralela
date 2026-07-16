RGM         := rgm47539
MAE         := 172.26.1.99
MAQUINAS    := l1m30u24 l1m14u24 l1m28u24
REMOTE_PATH := /home/local/rgm47539/mpi
parBIN      := IgorRoberto_47539_paralelo
parFONTE    := IgorRoberto_47539_paralelo.c
seqBIN      := IgorRoberto_47539_sequencial
seqFONTE    := IgorRoberto_47539_sequencial.c
HOSTFILE    := hosts
NP          := 4
GRAFO       := /tmp/graph.bin
ARGS        := 384.4912
CHAVE       := $(HOME)/.ssh/id_rsa
SSH_OPTS    := -o StrictHostKeyChecking=no
SCP_OPTS    := $(SSH_OPTS) -o BatchMode=yes

ifeq (run,$(firstword $(MAKECMDGOALS)))
RUN_NP := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
ifneq ($(RUN_NP),)
NP := $(RUN_NP)
$(eval $(RUN_NP):;@true)
endif
endif

SHELL := /bin/bash
.ONESHELL:
.DEFAULT_GOAL := all
.PHONY: all keys compila envia run seq run-seq clean

all: compila envia

keys:
	@set -e
	test -f $(CHAVE) || ssh-keygen -t rsa -b 4096 -N "" -f $(CHAVE)
	for m in $(MAE) $(MAQUINAS); do
	echo "--- $$m ---"
	ssh-copy-id $(SSH_OPTS) -i $(CHAVE).pub "$(RGM)@$$m" || echo "  falhou: $$m"
	done

compila: $(parBIN)

$(parBIN): $(parFONTE)
	mpicc $(parFONTE) -o $(parBIN) -O2 

envia: $(parBIN)
	@set -e
	mkdir -p $(REMOTE_PATH)
	[ "$$(readlink -f $(parBIN))" = "$$(readlink -f $(REMOTE_PATH)/$(parBIN))" ] || cp -f $(parBIN) $(REMOTE_PATH)/
	for m in $(MAQUINAS); do echo "--- $$m ---"; ssh $(SCP_OPTS) "$(RGM)@$$m" "mkdir -p $(REMOTE_PATH)"; scp $(SCP_OPTS) $(parBIN) "$(RGM)@$$m:$(REMOTE_PATH)/"; done

$(HOSTFILE): Makefile
	@set -e
	: > $(HOSTFILE)
	echo "$(MAE) slots=1" >> $(HOSTFILE)
	for m in $(MAQUINAS); do echo "$$m slots=1" >> $(HOSTFILE); done

run: $(HOSTFILE)
	@set -e
	lamnodes >/dev/null 2>&1 || lamboot -v $(HOSTFILE)
	mpirun -np $(NP) --hostfile $(HOSTFILE) --mca btl_tcp_if_include eth0 --mca oob_tcp_if_include eth0 ./$(parBIN) $(GRAFO) $(ARGS)

seq: $(seqBIN)

$(seqBIN): $(seqFONTE)
	gcc $(seqFONTE) -o $(seqBIN) -O2 

run-seq: $(seqBIN)
	./$(seqBIN) $(GRAFO)

clean:
	rm -f $(parBIN) $(seqBIN)