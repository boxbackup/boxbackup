<?xml version='1.0'?> 
<xsl:stylesheet  
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0"> 

<xsl:import href="http://docbook.sourceforge.net/release/xsl/current/html/chunk.xsl"/> 

<xsl:param name="html.stylesheet" select="'../html/bbdoc.css'"/>
<xsl:param name="chunk.section.depth" select="'0'"/>
<xsl:template name="user.header.navigation">
<div id="header">
<div id="logo">
<img src="../html/images/bblogo.png" alt="logo" height="65" width="331" border="0" vspace="5" align="middle" /> <img src="../html/images/stepahead.png" alt="a step ahead in data security" width="182" height="11" hspace="10" vspace="20" border="0" align="middle" /></div>
</div>
</xsl:template>


</xsl:stylesheet>
