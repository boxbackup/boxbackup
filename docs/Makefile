

# Process DocBook to HTML

DBPROC=xsltproc
BOOKXSL=bb-book.xsl
NOCHUNKBOOKXSL=bb-nochunk-book.xsl
MANXSL=bb-man.xsl
HTMLPREFIX=box-html
VPATH= adminguide
.SUFFIXES: .html .xml .1 .5 .8

all: docs

docs: instguide adminguide manpages
	@mkdir -p $(HTMLPREFIX)/images
	@cp html/images/*.png $(HTMLPREFIX)/images/.
	@cp html/*.css $(HTMLPREFIX)/.

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
	@if [ -f /usr/local/share/xsl/docbook/manpages/docbook.xsl ]; then \
	   DOCBOOK=file:///usr/local/share/xsl/docbook/manpages/docbook.xsl; \
	 elif [ -f /opt/local/share/xsl/docbook-xsl/manpages/docbook.xsl ]; then \
	   DOCBOOK=file:///opt/local/share/xsl/docbook-xsl/manpages/docbook.xsl; \
	 else \
	   DOCBOOK=http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl; \
	 fi; \
	 sed -e "s,%%DOCBOOK%%,$${DOCBOOK}," $(MANXSL).tmpl > $(MANXSL)

man-dirs: man/.there $(HTMLPREFIX)/man-html/.there

$(HTMLPREFIX)/man-html/.there:
	if [ ! -d $(HTMLPREFIX)/man-html ]; then mkdir -p $(HTMLPREFIX)/man-html; touch $(HTMLPREFIX)/man-html/.there; fi

man/.there:
	if [ ! -d man ]; then mkdir man; touch man/.there; fi

man-nroff: bbackupd.8 bbackupd-config.8 bbackupctl.8 bbackupquery.8 \
	bbstored.8 bbstored-config.8 bbstoreaccounts.8 bbstored-certs.8 \
	raidfile-config.8 \
	bbackupd.conf.5 bbstored.conf.5 raidfile.conf.5

man-html: bbackupd.html bbackupd-config.html bbackupctl.html bbackupquery.html \
	bbstored.html bbstored-config.html bbstoreaccounts.html bbstored-certs.html \
	raidfile-config.html \
	bbackupd.conf.html bbstored.conf.html raidfile.conf.html

.xml.html:
	@$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
	@cp $@ $(HTMLPREFIX)/man-html/.

.xml.8 .xml.5: $(MANXSL)
	@$(DBPROC) -o $@ $(MANXSL) $<
	@cp $@ man/
	@rm -f man/$@.gz
	@gzip -f -9 man/$@

dockit: clean docs
	tar zcf documentation-kit-0.10.tar.gz $(HTMLPREFIX)/

clean:
	if [ -d ./$(HTMLPREFIX) ]; then rm -rf $(HTMLPREFIX) ; fi
	if [ -d ./man ]; then  rm -rf ./man/; fi
	if [ -f ExceptionCodes.xml ]; then rm ExceptionCodes.xml; fi
	if [ -f documentation-kit-0.10.tar.gz ]; then rm documentation-kit-0.10.tar.gz; fi


