

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

manpages: man-dirs man-nroff man-html

man-dirs: man-pages/.there $(HTMLPREFIX)/man-html/.there

$(HTMLPREFIX)/man-html/.there:
	if [ ! -d man-html ]; then mkdir -p $(HTMLPREFIX)/man-html; touch $(HTMLPREFIX)/man-html/.there; fi

man-pages/.there:
	if [ ! -d man-pages ]; then mkdir man-pages; touch man-pages/.there; fi

man-nroff: bbackupquery.8 bbackupctl.8 bbstoreaccounts.8 bbstored-config.8 \
	raidfile-config.8 bbstored-certs.8

man-html: bbackupquery.html bbackupctl.html bbstoreaccounts.html \
	bbstored-config.html raidfile-config.html bbstored-certs.html

# for BSD make:
.xml.html:
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
	cp $@ $(HTMLPREFIX)/man-html/.

# for GNU make:
#%.html: %.xml
#	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
#	mv $@ $(HTMLPREFIX)/man-html/.

# for BSD make:
.xml.8:
	$(DBPROC) -o $(.TARGET) $(MANXSL) $(.IMPSRC)
	cp $(.TARGET) man-pages/
	rm -f man-pages/$(.TARGET).gz
	gzip -f -9 man-pages/$(.TARGET)

# for GNU make:
#%.8: %.xml
#	$(DBPROC) -o $@ $(MANXSL) $<
#	cp $@ man-pages/
#	rm -f man-pages/$@.gz
#	gzip -f -9 man-pages/$@

dockit: clean docs
	tar zcf documentation-kit-0.10.tar.gz $(HTMLPREFIX)/

clean:
	if [ -d ./$(HTMLPREFIX) ]; then rm -rf $(HTMLPREFIX) ; fi
	if [ -d ./man-pages ]; then  rm -rf ./man-pages/; fi
	if [ -f ExceptionCodes.xml ]; then rm ExceptionCodes.xml; fi
	if [ -f documentation-kit-0.10.tar.gz ]; then rm documentation-kit-0.10.tar.gz; fi


