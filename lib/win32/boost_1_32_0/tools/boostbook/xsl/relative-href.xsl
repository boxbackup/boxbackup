<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">

<xsl:import href="http://docbook.sourceforge.net/release/xsl/current/lib/lib.xsl"/>

<!-- ==================================================================== -->

<xsl:template name="href.target.relative">
    <xsl:param name="target"/>
    <xsl:param name="context" select="."/>

    <xsl:variable name="href.to.uri" select="$target"/>
    <xsl:variable name="href.from.uri">
        <xsl:call-template name="href.target.uri">
            <xsl:with-param name="object" select="$context"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="href.to">
        <xsl:call-template name="trim.common.uri.paths">
            <xsl:with-param name="uriA" select="$href.to.uri"/>
            <xsl:with-param name="uriB" select="$href.from.uri"/>
            <xsl:with-param name="return" select="'A'"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="href.from">
        <xsl:call-template name="trim.common.uri.paths">
            <xsl:with-param name="uriA" select="$href.to.uri"/>
            <xsl:with-param name="uriB" select="$href.from.uri"/>
            <xsl:with-param name="return" select="'B'"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="depth">
        <xsl:call-template name="count.uri.path.depth">
            <xsl:with-param name="filename" select="$href.from"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="href">
        <xsl:call-template name="copy-string">
            <xsl:with-param name="string" select="'../'"/>
            <xsl:with-param name="count" select="$depth"/>
        </xsl:call-template>
        <xsl:value-of select="$href.to"/>
    </xsl:variable>

    <xsl:value-of select="$href"/>

</xsl:template>

</xsl:stylesheet>
