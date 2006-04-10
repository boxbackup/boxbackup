

# Process DocBook to HTML

DBPROC=/usr/bin/xsltproc
BOOKXSL=bb-book.xsl
NOCHUNKBOOKXSL=bb-nochunk-book.xsl
MANXSL=bb-man.xsl
VPATH= adminguide
.SUFFIXES: .html .xml

all: adminguide instguide manpages

adminguide: adminguide/index.html 

adminguide/index.html: adminguide.xml ExceptionCodes.xml $(BOOKXSL)
	# docname=`echo $@ | sed -e 's/\/index.html//'`
	$(DBPROC) -o adminguide/ $(BOOKXSL) adminguide.xml

instguide: instguide/index.html 

instguide/index.html: instguide.xml $(BOOKXSL)
	$(DBPROC) -o instguide/ $(BOOKXSL) instguide.xml

ExceptionCodes.xml: ../../ExceptionCodes.txt
	perl ./generate_except_xml.pl

manpages: man-dirs man-nroff man-html

man-dirs: man-pages/.there man-html/.there

man-html/.there:
	if [ ! -d man-html ]; then mkdir man-html; touch man-html/.there; fi

man-pages/.there:
	if [ ! -d man-pages ]; then mkdir man-pages; touch man-pages/.there; fi

man-nroff: bbackupquery.1 bbackupctl.1

man-html: bbackupquery.html bbackupctl.html

%.html: %.xml
	$(DBPROC) -o $@ $(NOCHUNKBOOKXSL) $<
	mv $@ man-html/.

%.1: %.xml
	$(DBPROC) -o $@ $(MANXSL) $<
	mv $@ man-pages/.

dockit: instguide adminguide manpages
	tar zcf documentation-kit-0.10.tar.gz html/ instguide/ adminguide/ man-html/ man-pages

clean:
	if [ -d ./instguide ]; then rm -rf ./instguide/; fi
	if [ -d ./adminguide ]; then rm -rf ./adminguide/; fi
	if [ -d ./man-html ]; then rm -rf ./man-html/; fi
	if [ -d ./man-pages ]; then  rm -rf ./man-pages/; fi
	if [ -f ExceptionCodes.xml ]; then rm ExceptionCodes.xml; fi
	if [ -f docuemtation-kit-0.10.tar.gz ]; then rm documentation-kit-0.10.tar.gz; fi


