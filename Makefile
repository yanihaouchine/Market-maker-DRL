.PHONY: pdf rapport presentation dreamer clean help

LATEX = pdflatex -interaction=nonstopmode -halt-on-error -output-directory=rapport

help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  pdf            Compile rapport, presentation and dreamer PDFs"
	@echo "  rapport        Compile rapport/rapport.pdf"
	@echo "  presentation   Compile rapport/presentation.pdf"
	@echo "  dreamer        Compile rapport/dreamer.pdf"
	@echo "  clean          Remove LaTeX auxiliary files (PDFs are kept)"

pdf: rapport presentation dreamer

# Two passes for the table of contents and references.
rapport presentation dreamer:
	@echo "Compiling $@.pdf..."
	@$(LATEX) rapport/$@.tex > /dev/null && $(LATEX) rapport/$@.tex > /dev/null \
		|| { tail -n 30 rapport/$@.log; exit 1; }
	@echo "rapport/$@.pdf compiled"

clean:
	rm -f rapport/*.aux rapport/*.log rapport/*.nav rapport/*.snm rapport/*.toc rapport/*.out rapport/*.bbl rapport/*.blg rapport/*.vrb
