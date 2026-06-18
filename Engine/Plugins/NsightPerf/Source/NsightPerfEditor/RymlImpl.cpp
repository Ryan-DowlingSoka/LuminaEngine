// Single translation unit that compiles the rapidyaml (ryml) implementation. NvPerfUtility's HUD
// data model parses its embedded YAML configurations through ryml's single-header amalgamation;
// every other TU includes ryml_all.hpp for declarations only, this one defines the implementation.
#define RYML_SINGLE_HDR_DEFINE_NOW
#include <ryml_all.hpp>
