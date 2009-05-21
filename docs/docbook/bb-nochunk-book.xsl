<?xml version='1.0'?> 
<xsl:stylesheet  
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0"> 

<xsl:import href="../xsl-generic/html/docbook.xsl"/> 

<xsl:param name="html.stylesheet" select="'../bbdoc-man.css'"/>
<xsl:param name="chunk.section.depth" select="'0'"/>
<xsl:template name="user.header.content">
<div id="header">
<div id="logo">
<img src="../images/bblogo.png" alt="logo" height="65" width="331" border="0" vspace="5" align="middle" /> <img src="../images/stepahead.png" alt="a step ahead in data security" width="182" height="11" hspace="10" vspace="20" border="0" align="middle" /></div>
</div>
</xsl:template>


</xsl:stylesheet>
