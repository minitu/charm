-include ../../common.mk
OPTS = -O3
CHARMC = ../../../bin/ampicc
MPICC = mpicc

all: mpibench allgather alltoall alltoall_VPtest
all-mpi: mpibench.mpi allgather.mpi alltoall.mpi

allgather: allgather.c
	$(CHARMC) -c allgather.c $(OPTS)
	$(CHARMC) -o allgather allgather.o $(OPTS) $(LIBS)

allgather.mpi: allgather.c
	$(MPICC) -c allgather.c $(OPTS)
	$(MPICC) -o allgather.mpi allgather.o $(OPTS) $(LIBS)

alltoall: alltoall.c
	$(CHARMC) -c alltoall.c $(OPTS)
	$(CHARMC) -o alltoall alltoall.o $(OPTS) $(LIBS)

alltoall.mpi: alltoall.c
	$(MPICC) -c alltoall.c $(OPTS)
	$(MPICC) -o alltoall.mpi alltoall.o $(OPTS) $(LIBS)

mpibench: mpibench.c
	$(CHARMC) -c mpibench.c $(OPTS)
	$(CHARMC) -o mpibench mpibench.o $(OPTS) $(LIBS)

mpibench.mpi: mpibench.c
	$(MPICC) -c mpibench.c $(OPTS)
	$(MPICC) -o mpibench.mpi mpibench.o $(OPTS) $(LIBS)

alltoall_VPtest: alltoall_VPtest.c
	$(CHARMC) -c alltoall_VPtest.c $(OPTS)
	$(CHARMC) -o alltoall_VPtest alltoall_VPtest.o $(OPTS) $(LIBS)

test: all
# mpibench disabled because it hangs
#	$(call run, +p4 ./mpibench +vp4)
#	$(call run, +p4 ./mpibench +vp8)
	$(call run, +p4 ./alltoall 1024 +vp4)
	$(call run, +p4 ./alltoall 1024 +vp8)
	$(call run, +p4 ./allgather 1024 +vp4)
	$(call run, +p4 ./allgather 1024 +vp8)
	$(call run, +p4 ./alltoall_VPtest 1024 +vp4)
	$(call run, +p4 ./alltoall_VPtest 1024 +vp8)

test-mpi: all-mpi
#	mpirun -n 4 ./mpibench.mpi
	mpirun -n 4 ./allgather.mpi 1024
	mpirun -n 4 ./alltoall.mpi 1024

testp: all
#	$(call run, +p$(P) ./mpibench +vp$(P) )
#	$(call run, +p$(P) ./mpibench +vp$$(( $(P) * 2 )) )
#	$(call run, +p$(P) ./mpibench +vp$$(( $(P) * 4 )) )
	$(call run, +p$(P) ./alltoall 1024 +vp$(P) )
	$(call run, +p$(P) ./alltoall 1024 +vp$$(( $(P) * 2 )) )
	$(call run, +p$(P) ./alltoall 1024 +vp$$(( $(P) * 4 )) )
	$(call run, +p$(P) ./allgather 1024 +vp$(P) )
	$(call run, +p$(P) ./allgather 1024 +vp$$(( $(P) * 2 )) )
	$(call run, +p$(P) ./allgather 1024 +vp$$(( $(P) * 4 )) )
	$(call run, +p$(P) ./alltoall_VPtest 1024 +vp$(P) )
	$(call run, +p$(P) ./alltoall_VPtest 1024 +vp$$(( $(P) * 2 )) )
	$(call run, +p$(P) ./alltoall_VPtest 1024 +vp$$(( $(P) * 4 )) )

clean:
	rm -rf *~ *.o alltoall alltoall.mpi alltoall_VPtest mpibench mpibench.mpi charmrun conv-host allgather allgather.mpi ampirun
