#include <memory>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <libxml/parser.h>
#include <libxslt/security.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <storage/content_volume_layout.h>

#include "third_party/libzip/src/lib/zip.h"
#include "third_party/zlib/contrib/minizip/ioapi.h"

namespace {

bool TestXslt() {
    constexpr char xml[] = "<items><item>B</item><item>A</item></items>";
    constexpr char xsl[] =
        "<xsl:stylesheet version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>"
        "<xsl:template match='/'><sorted><xsl:for-each select='items/item'>"
        "<xsl:sort select='.'/><xsl:value-of select='.'/>"
        "</xsl:for-each></sorted></xsl:template></xsl:stylesheet>";
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>
        document(xmlReadMemory(xml, sizeof(xml) - 1, "input.xml", nullptr, XML_PARSE_NONET),
                 xmlFreeDoc);
    xmlDocPtr stylesheet_document =
        xmlReadMemory(xsl, sizeof(xsl) - 1, "sort.xsl", nullptr, XML_PARSE_NONET);
    if (!document || !stylesheet_document) {
        if (stylesheet_document) xmlFreeDoc(stylesheet_document);
        return false;
    }
    std::unique_ptr<xsltStylesheet, decltype(&xsltFreeStylesheet)>
        stylesheet(xsltParseStylesheetDoc(stylesheet_document), xsltFreeStylesheet);
    if (!stylesheet) {
        xmlFreeDoc(stylesheet_document);
        return false;
    }
    std::unique_ptr<xsltSecurityPrefs, decltype(&xsltFreeSecurityPrefs)>
        security(xsltNewSecurityPrefs(), xsltFreeSecurityPrefs);
    std::unique_ptr<xsltTransformContext, decltype(&xsltFreeTransformContext)>
        context(xsltNewTransformContext(stylesheet.get(), document.get()),
                xsltFreeTransformContext);
    if (!security || !context ||
        xsltSetSecurityPrefs(security.get(), XSLT_SECPREF_READ_FILE, xsltSecurityForbid) ||
        xsltSetSecurityPrefs(security.get(), XSLT_SECPREF_READ_NETWORK, xsltSecurityForbid) ||
        xsltSetCtxtSecurityPrefs(security.get(), context.get()))
        return false;
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>
        result(xsltApplyStylesheetUser(stylesheet.get(), document.get(), nullptr,
                                       nullptr, nullptr, context.get()),
               xmlFreeDoc);
    if (!result) return false;
    xmlNodePtr root = xmlDocGetRootElement(result.get());
    if (!root || !xmlStrEqual(root->name, BAD_CAST "sorted")) return false;
    xmlChar* text = xmlNodeGetContent(root);
    const bool sorted = text && xmlStrEqual(text, BAD_CAST "AB");
    if (text) xmlFree(text);
    return sorted &&
        xsltCheckRead(security.get(), context.get(), BAD_CAST "file:///content/index.html") == 0 &&
        xsltCheckRead(security.get(), context.get(), BAD_CAST "http://example.invalid/") == 0;
}

bool TestArchiveInput() {
    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/bear-vp9.webm";
    constexpr unsigned char expected[] = {0x1a, 0x45, 0xdf, 0xa3};
    zip_error_t error;
    zip_error_init(&error);
    std::unique_ptr<zip_source_t, decltype(&zip_source_free)>
        source(zip_source_file_create(path, 0, -1, &error), zip_source_free);
    zip_error_fini(&error);
    if (!source) return false;
    zip_stat_t stat{};
    zip_file_attributes_t attributes{};
    if (zip_source_stat(source.get(), &stat) || !(stat.valid & ZIP_STAT_SIZE) ||
        stat.size != 67504 || zip_source_get_file_attributes(source.get(), &attributes) ||
        (attributes.valid & ZIP_FILE_ATTRIBUTES_EXTERNAL_FILE_ATTRIBUTES))
        return false;

    zlib_filefunc64_def callbacks{};
    fill_fopen64_filefunc(&callbacks);
    voidpf file = callbacks.zopen64_file(callbacks.opaque, path,
                                         ZLIB_FILEFUNC_MODE_READ | ZLIB_FILEFUNC_MODE_EXISTING);
    if (!file) return false;
    unsigned char bytes[sizeof(expected)]{};
    const bool valid =
        callbacks.zread_file(callbacks.opaque, file, bytes, sizeof(bytes)) == sizeof(bytes) &&
        memcmp(bytes, expected, sizeof(bytes)) == 0 &&
        callbacks.ztell64_file(callbacks.opaque, file) == sizeof(bytes) &&
        callbacks.zseek64_file(callbacks.opaque, file, 0, ZLIB_FILEFUNC_SEEK_END) == 0 &&
        callbacks.ztell64_file(callbacks.opaque, file) == 67504 &&
        callbacks.zseek64_file(callbacks.opaque, file, 1ULL << 32, ZLIB_FILEFUNC_SEEK_SET) != 0;
    return callbacks.zclose_file(callbacks.opaque, file) == 0 && valid;
}

bool TestArchivePublication() {
    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-libzip-probe";
    constexpr unsigned char input[] = {0x01, 0x00, 0xf9, 0x44};
    zip_error_t error;
    zip_error_init(&error);
    std::unique_ptr<zip_source_t, decltype(&zip_source_free)>
        source(zip_source_file_create(path, 0, -1, &error), zip_source_free);
    zip_error_fini(&error);
    if (!source || zip_source_begin_write(source.get()) ||
        zip_source_write(source.get(), input, sizeof(input)) != sizeof(input))
        return false;
    zip_source_rollback_write(source.get());
    struct stat status{};
    if (stat(path, &status) != -1 || errno != ENOENT ||
        zip_source_begin_write(source.get()) ||
        zip_source_write(source.get(), input, sizeof(input)) != sizeof(input) ||
        zip_source_commit_write(source.get()) || zip_source_open(source.get()))
        return false;
    unsigned char output[sizeof(input)]{};
    const bool valid =
        zip_source_read(source.get(), output, sizeof(output)) == sizeof(output) &&
        memcmp(output, input, sizeof(input)) == 0 &&
        stat(path, &status) == 0 && status.st_size == sizeof(input);
    return zip_source_close(source.get()) == 0 && valid;
}

}  // namespace

int TestXmlArchiveConsumers() {
    time_t saved{};
    const time_t now = time(&saved);
    if (now < 946684800 || now != saved) return 80;
    if (!TestXslt()) return 81;
    if (!TestArchiveInput()) return 82;
    if (!TestArchivePublication()) return 83;
    return 0;
}
