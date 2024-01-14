builddir := build
targets := $(addprefix $(builddir)/,bst rbtree hashtable)

all: $(targets)

$(targets): $(builddir)/%: %.c
	$(CC) -o $@ $<

$(targets): | $(builddir)

$(builddir):
	mkdir $@

clean:
	rm -r $(builddir)
