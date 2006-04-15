

# Process DocBook to HTML

DBPROC=/usr/bin/xsltproc
BOOKXSL=bb-book.xsl
NOCHUNKBOOKXSL=bb-nochunk-book.xsl
MANXSL=bb-man.xsl
HTMLPREFIX=box-html
VPATH= adminguide
.SUFFIXES: .html .xml

all: adminguide instguide manpages 

adminguide: $(HTMLPREFIX)/adminguide/index.html 

$(HTMLPREFIX)/adminguide/index.html: adminguide.xml ExceptionCodes.xml $(BOOKXSL)
	# docname=`echo $@ | sed -e 's/\/index.html//'`
	$(DBPROC) -o $(HTMLPREFIX)/adminguide/ $(BOOKXSL) adminguide.xml

instguide: $(HTMLPREFIX)/instguide/index.html 

$(HTMLPREFIX)/instguide/index.html: instguide.xml $(BOOKXSL)
	$(DBPROC) -o $(HTMLPREFIX)/instguide/ $(BOOKXSL) instguide.xml

ExceptionCodes.xml: ../../ExceptionCodes.txt
	perl ./generate_except_xml.pl

manpages: man-dirs man-nroff man-html

man-dirs: man-pages/.there $(HTMLPREFIX)/man-html/.there

$(HTMLPREFIX)/man-html/.there:
	if [ ! -d man-html ]; then mkdir -p $(HTMLPREFIX)/man-html; touch $(HTMLPREFIX)/man-html/.there; fi

man-pages/.there:
	if [ ! -d man-pages ]; then mkdir man-pages; touch man-pages/.there; fi

man-nroff: bbackupquery.1 bbackupctl.1 bbstoreaccounts.1

man-html: bbackupquery.html bbackupctl.html bbstoreaccounts.html

%.html: %.xml
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
	mv $@ $(HTMLPREFIX)/man-html/.

%.1: %.xml
	$(DBPROC) -o $@ $(MANXSL) $<
	mv $@ man-pages/.
	gzip -f -9 man-pages/$@

dockit: instguide adminguide manpages
	mkdir $(HTMLPREFIX)/images
	cp html/images/*.png $(HTMLPREFIX)/images/.
	cp html/*.css $(HTMLPREFIX)/.
	tar zcf documentation-kit-0.10.tar.gz $(HTMLPREFIX)/

clean:
	if [ -d ./$(HTMLPREFIX) ]; then rm -rf $(HTMLPREFIX) ; fi
	if [ -d ./man-pages ]; then  rm -rf ./man-pages/; fi
	if [ -f ExceptionCodes.xml ]; then rm ExceptionCodes.xml; fi
	if [ -f documentation-kit-0.10.tar.gz ]; then rm documentation-kit-0.10.tar.gz; fi


