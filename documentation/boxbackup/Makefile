

# Process DocBook to HTML

DBPROC=/usr/bin/xsltproc
BOOKXSL=bb-book.xsl
MANXSL= /usr/share/sgml/docbook/xsl-stylesheets/manpages/docbook.xsl
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

manpages: bbackupquery.html bbackupquery.1 

bbackupquery.html: bbackupquery.xml
	$(DBPROC) -o man-html/bbackupquery.html $(BOOKXSL) bbackupquery.xml

bbackupquery.1: bbackupquery.xml
	$(DBPROC) -o man-pages/bbackupquery.1 $(MANXSL) bbackupquery.xml

dockit: instguide adminguide
	tar zcf documentation-kit-0.10.tar.gz html/ instguide/ adminguide/

clean:
	rm -rf ./instguide/
	rm -rf ./adminguide/
	rm ExceptionCodes.xml
	rm documentation-kit-0.10.tar.gz


# %.html: %.xml
# 	$(DBPROC) -o $@ $(BOOKXSL) $@.xml


