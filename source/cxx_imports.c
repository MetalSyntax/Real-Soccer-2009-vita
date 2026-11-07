// Generado por tools/gen_cxx.py: imports C++ del juego -> libstdc++ de VitaSDK.
// No editar a mano (volver a correr gen_cxx.py con el report.txt de ios_prep.py).
#include <string.h>

#include "imports.h"

extern char cxx_0 __asm__("_ZSt17__throw_bad_allocv"); // std::__throw_bad_alloc()
extern char cxx_1 __asm__("_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base"); // std::_Rb_tree_increment(std::_Rb_tree_node_base*)
extern char cxx_2 __asm__("_ZSt20__throw_length_errorPKc"); // std::__throw_length_error(char const*)
extern char cxx_3 __asm__("_ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_"); // std::_Rb_tree_rebalance_for_erase(std::_Rb_tree_node_base*, std::_Rb_tree_node_base&)
extern char cxx_4 __asm__("_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_"); // std::_Rb_tree_insert_and_rebalance(bool, std::_Rb_tree_node_base*, std::_Rb_tree_node_base*, std::_Rb_tree_node_base&)
extern char cxx_5 __asm__("_ZTVN10__cxxabiv117__class_type_infoE"); // vtable for __cxxabiv1::__class_type_info
extern char cxx_6 __asm__("_ZTVN10__cxxabiv120__si_class_type_infoE"); // vtable for __cxxabiv1::__si_class_type_info
extern char cxx_7 __asm__("_ZTVN10__cxxabiv121__vmi_class_type_infoE"); // vtable for __cxxabiv1::__vmi_class_type_info
extern char cxx_8 __asm__("_ZdaPv"); // operator delete[](void*)
extern char cxx_9 __asm__("_ZdlPv"); // operator delete(void*)
extern char cxx_10 __asm__("_Znaj"); // operator new[](unsigned long)
extern char cxx_11 __asm__("_Znwj"); // operator new(unsigned long)

const import_entry cxx_imports[] = {
    {"__ZSt17__throw_bad_allocv", (uintptr_t)&cxx_0, 0},
    {"__ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base", (uintptr_t)&cxx_1, 0},
    {"__ZSt20__throw_length_errorPKc", (uintptr_t)&cxx_2, 0},
    {"__ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_", (uintptr_t)&cxx_3, 0},
    {"__ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_", (uintptr_t)&cxx_4, 0},
    {"__ZTVN10__cxxabiv117__class_type_infoE", (uintptr_t)&cxx_5, 1},
    {"__ZTVN10__cxxabiv120__si_class_type_infoE", (uintptr_t)&cxx_6, 1},
    {"__ZTVN10__cxxabiv121__vmi_class_type_infoE", (uintptr_t)&cxx_7, 1},
    {"__ZdaPv", (uintptr_t)&cxx_8, 0},
    {"__ZdlPv", (uintptr_t)&cxx_9, 0},
    {"__Znam", (uintptr_t)&cxx_10, 0},
    {"__Znwm", (uintptr_t)&cxx_11, 0},
};
TABLE_END(cxx)

// Lo que no tiene equivalente queda en el log como "unimpl".
const import_entry *cxx_import(const char *name) { (void)name; return NULL; }
