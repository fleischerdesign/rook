#include "application.hpp"
#include <glib/gi18n.h>
#include <peel/Gio/Gio.h>
#include <gio/gio.h>
#include <clocale>

using namespace peel;

static gchar* find_localedir(const char *argv0) {
    g_autofree gchar *exe_dir = g_path_get_dirname(argv0);
    g_autofree gchar *po_dir = g_build_filename(exe_dir, "..", "..", "po", nullptr);
    g_autofree gchar *mo = g_build_filename(po_dir, "de", "LC_MESSAGES", "rook.mo", nullptr);

    if (g_file_test(mo, G_FILE_TEST_EXISTS))
        return g_strdup(po_dir);

    return nullptr;
}

static void setup_schema_dir(const char *argv0) {
    if (g_getenv("GSETTINGS_SCHEMA_DIR"))
        return;

    g_autofree gchar *exe_dir = g_path_get_dirname(argv0);
    g_autofree gchar *data_dir = g_build_filename(exe_dir, "..", "..", "data", nullptr);
    g_autofree gchar *compiled = g_build_filename(data_dir, "gschemas.compiled", nullptr);

    if (g_file_test(compiled, G_FILE_TEST_EXISTS))
        g_setenv("GSETTINGS_SCHEMA_DIR", data_dir, true);
}

int main(int argc, char* argv[]) {
    setup_schema_dir(argv[0]);
    setlocale(LC_ALL, "");

    auto settings = Gio::Settings::create("io.github.fleischerdesign.Rook");
    auto lang = settings->get_string("language");
    if (g_strcmp0(lang, "de") == 0)
        g_setenv("LANGUAGE", "de", true);
    else
        g_setenv("LANGUAGE", "en", true);

    g_autofree gchar *dev_localedir = find_localedir(argv[0]);
    bindtextdomain(GETTEXT_PACKAGE, dev_localedir ? dev_localedir : LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    auto app = rook::gui::RookApplication::create();
    return app->run(argc, argv);
}
