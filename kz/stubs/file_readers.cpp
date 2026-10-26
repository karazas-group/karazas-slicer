// The pricer reads 3MF and OBJ itself (three.js) and hands the slicer a
// binary STL, so Orca's own file readers are never called. Model.cpp still
// references them from Model::read_from_file, which pulls in the 3MF/AMF/OBJ
// parsers, the preset system, SVG/Emboss, PNG and zip. These stand in for
// them so the linker leaves all of that out.
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/AMF.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Format/svg.hpp"
#include "libslic3r/Format/DRC.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

namespace Slic3r {
bool load_obj(const char *, Model *, ObjInfo &, std::string &message, const char *) { message = "not built in"; return false; }
bool load_svg(const char *, Model *, std::string &message) { message = "not built in"; return false; }
bool load_drc(const char *, Model *, const char *) { return false; }
bool load_amf(const char *, DynamicPrintConfig *, ConfigSubstitutionContext *, Model *, bool *) { return false; }
bool load_3mf(const char *, DynamicPrintConfig &, ConfigSubstitutionContext &, Model *, bool) { return false; }
bool PrusaFileParser::check_3mf_from_prusa(const std::string) { return false; }
bool load_bbs_3mf(const char *, DynamicPrintConfig *, ConfigSubstitutionContext *, Model *, PlateDataPtrs *, std::vector<Preset *> *,
                  bool *, bool *, Semver *, Import3mfProgressFn, LoadStrategy, BBLProject *, int) { return false; }
void delete_object_mesh(ModelObject &) {}
void save_object_mesh(ModelObject &) {}
void remove_backup(Model &, bool) {}
}
