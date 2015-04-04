# Process DocBook to HTML

# This makefile is a bit obfuscated so that it works correctly on both
# BSD and GNU make. Some parts apply to one version of make and not the
# other; these are marked by comments.

# The "all" target shouldn't be up here, but the trickery below defines
# what looks like a rule to GNU make, and so we need to define the actual
# default target before it.

all: docs

DBPROC_COMMAND = xsltproc
MKDIR_COMMAND  = mkdir
CP_COMMAND     = cp
PERL_COMMAND   = perl
RM_COMMAND     = rm -f
TAR_COMMAND    = tar
GZIP_COMMAND   = gzip -f
GENERATE_SCRIPT = tools/generate_except_xml.pl

DBPROC   = $(DBPROC_COMMAND)
MKDIR    = $(MKDIR_COMMAND)
CP       = $(CP_COMMAND)
GENERATE = $(PERL_COMMAND) $(GENERATE_SCRIPT)
RM_QUIET = $(RM_COMMAND)
TAR      = $(TAR_COMMAND)
GZIP     = $(GZIP_COMMAND)
PROGRESS = @ true

# use a GNU make "define" command, that looks like a harmless dummy rule
# to BSD make, to hide parts of the Makefile from GNU make.
define IGNORED_BY_GNU_MAKE:
.if 0
endef

  # seen by GNU make, not by BSD make
  ifeq ($(V),)
    DBPROC   = @ echo "  [XLSTPROC]" $^ && $(DBPROC_COMMAND) 2>/dev/null
    GENERATE = @ echo "  [GENERATE]" $@ && $(PERL_COMMAND) $(GENERATE_SCRIPT)
    TAR      = @ echo "  [TAR]     " $@ && $(TAR_COMMAND)
    GZIP     = @ echo "  [GZIP]    " $< && $(GZIP_COMMAND)
    RM_QUIET = @ $(RM_COMMAND)
    PROGRESS = @ echo
  endif  

define IGNORED_BY_GNU_MAKE:
.endif

.ifndef V
  # seen by BSD make, not by GNU make
  DBPROC   = @ echo "  [XSLTPROC]" $(.ALLSRC) && $(DBPROC_COMMAND) 2>/dev/null
  GENERATE = @ echo "  [GENERATE]" $(.TARGET) && $(PERL_COMMAND) $(GENERATE_SCRIPT)
  TAR      = @ echo "  [TAR]     " $(.TARGET) && $(TAR_COMMAND)
  GZIP     = @ echo "  [GZIP]    " $(.TARGET:.gz=) && $(GZIP_COMMAND)
  RM_QUIET = @ $(RM_COMMAND)
  PROGRESS = @ echo
.endif

# neither .endif nor endef can be followed by a colon; each creates
# warnings or errors in one or other version of make. we need some
# magic to make them both work. Luckily, .endfor ignores the colon.

.for DUMMY in $(NO_SUCH_VARIABLE)
endef
.endfor :

PROGRESS_RM = $(PROGRESS) "  [RM]      "

DOCBOOK_DIR = docbook
HTML_DIR = htmlguide
MAN_DIR = man

BOOKXSL = $(DOCBOOK_DIR)/bb-book.xsl
NOCHUNKBOOKXSL = $(DOCBOOK_DIR)/bb-nochunk-book.xsl
MANXSL = $(DOCBOOK_DIR)/bb-man.xsl

VPATH = $(DOCBOOK_DIR)
.SUFFIXES: .html .xml .gz .1 .5 .8

docs: instguide adminguide manpages
	@mkdir -p $(HTML_DIR)/images
	@cp $(DOCBOOK_DIR)/html/images/*.png $(HTML_DIR)/images/.
	@cp $(DOCBOOK_DIR)/html/*.css $(HTML_DIR)/.
	@cp $(DOCBOOK_DIR)/html/*.ico $(HTML_DIR)/.

adminguide: $(DOCBOOK_DIR)/ExceptionCodes.xml $(HTML_DIR)/adminguide/index.html 

# $^ gives all sources on GNU make, and nothing on BSD make
# $> gives all sources on BSD make, and nothing on GNU make
$(HTML_DIR)/adminguide/index.html: $(BOOKXSL) $(DOCBOOK_DIR)/adminguide.xml
	$(DBPROC) -o $(HTML_DIR)/adminguide/ $^ $>

instguide: $(HTML_DIR)/instguide/index.html 

$(HTML_DIR)/instguide/index.html: $(BOOKXSL) $(DOCBOOK_DIR)/instguide.xml
	$(DBPROC) -o $(HTML_DIR)/instguide/ $^ $>

# On BSD make, $> contains all sources and $^ is empty 
# On GNU make, $^ contains all sources and $> is empty
$(DOCBOOK_DIR)/ExceptionCodes.xml: ../ExceptionCodes.txt
	$(GENERATE) $> $^ $@

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

# $^ gives all sources on GNU make, and nothing on BSD make

# GNU make
$(HTML_DIR)/man-html/%.html: $(NOCHUNKBOOKXSL) $(DOCBOOK_DIR)/%.xml
	$(DBPROC) -o $@ $^

# GNU make
$(MAN_DIR)/%.8: $(MANXSL) $(DOCBOOK_DIR)/%.xml
	$(DBPROC) -o $@ $^

# GNU make
$(MAN_DIR)/%.8.gz: $(MAN_DIR)/%.8
	$(GZIP) $<

# GNU make
$(MAN_DIR)/%.5: $(MANXSL) $(DOCBOOK_DIR)/%.xml $(MANXSL)
	$(DBPROC) -o $@ $^

# GNU make
$(MAN_DIR)/%.5.gz: $(MAN_DIR)/%.5
	$(GZIP) $<

# BSD make: the final colon (:) is required to make the .for and .endfor
# lines valid in GNU make. It creates (different) dummy rules in GNU and
# BSD make. Both dummy rules are harmless.

.for MAN_PAGE in $(NROFF_PAGES) :
$(MAN_DIR)/$(MAN_PAGE).gz: $(MANXSL) $(DOCBOOK_DIR)/$(MAN_PAGE:R).xml
	$(DBPROC) -o $(.TARGET:.gz=) $(.ALLSRC)
	$(GZIP) $(.TARGET:.gz=)

$(HTML_DIR)/man-html/$(MAN_PAGE:R).html: $(NOCHUNKBOOKXSL) \
$(DOCBOOK_DIR)/$(MAN_PAGE:R).xml
	$(DBPROC) -o $(.TARGET) $(.ALLSRC)
.endfor :

dockit: clean docs documentation-kit-0.10.tar.gz

documentation-kit-0.10.tar.gz:
	$(TAR) zcf documentation-kit-0.10.tar.gz $(HTML_DIR)/

clean:
	$(PROGRESS_RM) "$(HTML_DIR)/man-html/*.html"
	$(RM_QUIET) $(HTML_FILES)

	$(PROGRESS_RM) "$(MAN_DIR)/*.[58].gz"
	$(RM_QUIET) $(NROFF_FILES)

	$(PROGRESS_RM) "$(DOCBOOK_DIR)/ExceptionCodes.xml"
	$(RM_QUIET) $(DOCBOOK_DIR)/ExceptionCodes.xml
	
	$(PROGRESS_RM) "documentation-kit-0.10.tar.gz"
	$(RM_QUIET) documentation-kit-0.10.tar.gz
