

# Process DocBook to HTML

DBPROC=/usr/bin/xsltproc
BOOKXSL=bb-book.xsl
MANXSL=bb-man.xsl
VPATH= adminguide
.SUFFIXES: .html .xml

all: adminguide instguide 

adminguide: adminguide/index.html 

adminguide/index.html: adminguide.xml ExceptionCodes.xml bb-book.xsl
	# docname=`echo $@ | sed -e 's/\/index.html//'`
	$(DBPROC) -o adminguide/ $(BOOKXSL) adminguide.xml

instguide: instguide/index.html bb-book.xsl

instguide/index.html: instguide.xml bb-book.xsl
	$(DBPROC) -o instguide/ $(BOOKXSL) instguide.xml

ExceptionCodes.xml: ../../ExceptionCodes.txt
	perl ./generate_except_xml.pl

dockit: instguide adminguide
	tar zcf documentation-kit-0.10.tar.gz html/ instguide/ adminguide/

clean:
	rm -rf ./instguide/
	rm -rf ./adminguide/
	rm documentation-kit-0.10.tar.gz


# %.html: %.xml
# 	$(DBPROC) -o $@ $(BOOKXSL) $@.xml


