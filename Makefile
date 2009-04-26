

# Process DocBook to HTML

DBPROC=xsltproc

DOCBOOK_DIR = docbook
HTML_DIR = htmlguide
MAN_DIR = man

BOOKXSL = $(DOCBOOK_DIR)/bb-book.xsl
NOCHUNKBOOKXSL = $(DOCBOOK_DIR)/bb-nochunk-book.xsl
MANXSL = $(DOCBOOK_DIR)/bb-man.xsl

VPATH = $(DOCBOOK_DIR)
.SUFFIXES: .html .xml .gz .1 .5 .8

all: docs

docs: instguide adminguide manpages
	@mkdir -p $(HTML_DIR)/images
	@cp $(DOCBOOK_DIR)/html/images/*.png $(HTML_DIR)/images/.
	@cp $(DOCBOOK_DIR)/html/*.css $(HTML_DIR)/.
	@cp $(DOCBOOK_DIR)/html/*.ico $(HTML_DIR)/.

adminguide: $(DOCBOOK_DIR)/ExceptionCodes.xml $(HTML_DIR)/adminguide/index.html 

# all sources ($>) is exactly the right args for xsltproc
$(HTML_DIR)/adminguide/index.html: $(BOOKXSL) $(DOCBOOK_DIR)/adminguide.xml
	$(DBPROC) -o $(HTML_DIR)/adminguide/ $>

instguide: $(HTML_DIR)/instguide/index.html 

$(HTML_DIR)/instguide/index.html: $(BOOKXSL) $(DOCBOOK_DIR)/instguide.xml
	$(DBPROC) -o $(HTML_DIR)/instguide/ $>

# $< is empty on BSD make when making this rule, $> has all sources
# $< has the target on GNU make, $> is empty
$(DOCBOOK_DIR)/ExceptionCodes.xml: ../ExceptionCodes.txt
	perl tools/generate_except_xml.pl $< $> $@

manpages: man-dirs man-nroff man-html

man-dirs: man/.there $(HTML_DIR)/man-html/.there

$(HTML_DIR)/man-html/.there:
	mkdir -p $(HTML_DIR)/man-html
	touch $(HTML_DIR)/man-html/.there

man/.there:
	mkdir -p man
	touch man/.there

NROFF_PAGES = bbackupd.8 bbackupd-config.8 bbackupctl.8 bbackupquery.8 \
	bbstored.8 bbstored-config.8 bbstoreaccounts.8 bbstored-certs.8 \
	raidfile-config.8 \
	bbackupd.conf.5 bbstored.conf.5 raidfile.conf.5

NROFF_FILES = $(NROFF_PAGES:%=$(MAN_DIR)/%.gz)

man-nroff: $(NROFF_FILES)

HTML_FILES_1 = $(NROFF_PAGES:%.5=%.html)
HTML_FILES_2 = $(HTML_FILES_1:%.8=%.html)
HTML_FILES   = $(HTML_FILES_2:%=$(HTML_DIR)/man-html/%)

man-html: $(HTML_FILES)

# GNU make
$(HTML_DIR)/man-html/%.html: $(DOCBOOK_DIR)/%.xml $(NOCHUNKBOOKXSL)
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<

# GNU make
$(MAN_DIR)/%.8.gz: $(DOCBOOK_DIR)/%.xml
	$(DBPROC) -o $(@:.gz=) $(MANXSL) $<
	gzip $(@:.gz=)

# GNU make
$(MAN_DIR)/%.5.gz: $(DOCBOOK_DIR)/%.xml
	$(DBPROC) -o $(@:.gz=) $(MANXSL) $<
	gzip $(@:.gz=)

# BSD make: the final colon (:) is required to make the .for and .endfor
# lines valid in GNU make. It creates (different) dummy rules in GNU and
# BSD make. Both dummy rules are harmless.

.for MAN_PAGE in $(NROFF_PAGES) :
$(MAN_DIR)/$(MAN_PAGE).gz: $(DOCBOOK_DIR)/$(MAN_PAGE:R).xml
	$(DBPROC) -o $(.TARGET:.gz=) $(MANXSL) $>
	gzip $(@:.gz=)

$(HTML_DIR)/man-html/$(MAN_PAGE:R).html: $(DOCBOOK_DIR)/$(MAN_PAGE:R).xml
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $>
.endfor :

dockit: clean docs
	tar zcf documentation-kit-0.10.tar.gz $(HTML_DIR)/

clean:
	rm -f $(HTML_FILES)
	rm -f $(NROFF_FILES)
	rm -f $(DOCBOOK_DIR)/ExceptionCodes.xml
	rm -f documentation-kit-0.10.tar.gz

