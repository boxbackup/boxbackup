

# Process DocBook to HTML

DBPROC=xsltproc
BOOKXSL=bb-book.xsl
NOCHUNKBOOKXSL=bb-nochunk-book.xsl
MANXSL=bb-man.xsl
HTMLPREFIX=box-html
VPATH= adminguide
# If your OS declares a system make variable, add a .elif statement here
# with the path to the locally-installed DocBook stylesheet
.if .FreeBSD
# Requires textproc/docbook-xsl port installed
DOCBOOK=file:///usr/local/share/xsl/docbook/manpages/docbook.xsl
.else
DOCBOOK=http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl
.endif
.SUFFIXES: .html .xml .1 .5 .8

all: docs

docs: instguide adminguide manpages
	mkdir -p $(HTMLPREFIX)/images
	cp html/images/*.png $(HTMLPREFIX)/images/.
	cp html/*.css $(HTMLPREFIX)/.

adminguide: $(HTMLPREFIX)/adminguide/index.html 

$(HTMLPREFIX)/adminguide/index.html: adminguide.xml ExceptionCodes.xml $(BOOKXSL)
	# docname=`echo $@ | sed -e 's/\/index.html//'`
	$(DBPROC) -o $(HTMLPREFIX)/adminguide/ $(BOOKXSL) adminguide.xml

instguide: $(HTMLPREFIX)/instguide/index.html 

$(HTMLPREFIX)/instguide/index.html: instguide.xml $(BOOKXSL)
	$(DBPROC) -o $(HTMLPREFIX)/instguide/ $(BOOKXSL) instguide.xml

ExceptionCodes.xml: ../ExceptionCodes.txt
	perl ./generate_except_xml.pl

manpages: $(MANXSL) man-dirs man-nroff man-html

$(MANXSL): $(MANXSL).tmpl
	@sed -e 's,%%DOCBOOK%%,$(DOCBOOK),' $(MANXSL).tmpl > $(MANXSL)

man-dirs: man-pages/.there $(HTMLPREFIX)/man-html/.there

$(HTMLPREFIX)/man-html/.there:
	if [ ! -d man-html ]; then mkdir -p $(HTMLPREFIX)/man-html; touch $(HTMLPREFIX)/man-html/.there; fi

man-pages/.there:
	if [ ! -d man-pages ]; then mkdir man-pages; touch man-pages/.there; fi

man-nroff: bbackupd.8 bbackupd-config.8 bbackupctl.8 bbackupquery.8 \
	bbstored.8 bbstored-config.8 bbstoreaccounts.8 bbstored-certs.8 \
	raidfile-config.8 \
	bbackupd.conf.5 bbstored.conf.5 raidfile.conf.5

man-html: bbackupd.html bbackupd-config.html bbackupctl.html bbackupquery.html \
	bbstored.html bbstored-config.html bbstoreaccounts.html bbstored-certs.html \
	raidfile-config.html \
	bbackupd.conf.html bbstored.conf.html raidfile.conf.html

.xml.html:
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
	cp $@ $(HTMLPREFIX)/man-html/.

.xml.8 .xml.5:
	$(DBPROC) -o $@ $(MANXSL) $<
	cp $@ man-pages/
	rm -f man-pages/$@.gz
	gzip -f -9 man-pages/$@

dockit: clean docs
	tar zcf documentation-kit-0.10.tar.gz $(HTMLPREFIX)/

clean:
	if [ -d ./$(HTMLPREFIX) ]; then rm -rf $(HTMLPREFIX) ; fi
	if [ -d ./man-pages ]; then  rm -rf ./man-pages/; fi
	if [ -f ExceptionCodes.xml ]; then rm ExceptionCodes.xml; fi
	if [ -f documentation-kit-0.10.tar.gz ]; then rm documentation-kit-0.10.tar.gz; fi


