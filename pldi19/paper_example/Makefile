
include variables

all:
	gcc -O1 -std=c99 source.c -c -o opt1.o
	gcc -O1 -std=c99 source.c -o opt1bin
	stoke extract -i opt1.o -o opt1

pdfs:
	stoke debug cfg --target $(TARGET) -o $(NAME).pdf
	stoke debug cfg --target $(REWRITE) -o $(NAME)_vec.pdf

tcgen:
	rm -rf tcs
	mkdir -p tcs
	stoke_tcgen --target $(TARGET) --bound 8 --def_in $(DEF_INS) --live_out $(LIVE_OUTS) --output tcs/tcgen1 --mutants 0
	stoke_tcgen --target $(REWRITE) --bound 8 --def_in $(DEF_INS) --live_out $(LIVE_OUTS) --output tcs/tcgen2 --mutants 0
	cat tcs/tcgen1 tcs/tcgen2 > testcases
	rm -rf tcs

clean:
	rm -rf opt1* opt3* *.pdf sage* stoke_sage* state* *.tmp tcs
