-include ../../common.mk
CHARMC=../../../bin/ampicxx $(OPTS)

all: migration

migration: test.o
	$(CHARMC) -o migration test.o

test.o: test.C
	$(CHARMC) -c test.C

#
#
# clean up .o, .mod, .exe and EMACS backup files
#
clean:
	rm -f *.o *.mod migration *~ conv-host charmrun test.o charmrun.exe migration.exe migration.pdb migration.ilk ampirun

test: migration
	$(call run, ./migration +p1 +vp1 )
	$(call run, ./migration +p1 +vp4 )
	$(call run, ./migration +p1 +vp20 )
	$(call run, ./migration +p2 +vp1 )
	$(call run, ./migration +p2 +vp4 )
	$(call run, ./migration +p2 +vp20 )

testp: migration
	$(call run, ./migration +p$(P) +vp$(P) )
	$(call run, ./migration +p$(P) +vp$$(( $(P) * 2 )) )
	$(call run, ./migration +p$(P) +vp$$(( $(P) * 4 )) )
