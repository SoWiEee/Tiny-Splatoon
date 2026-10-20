#ifdef _WIN32
// Ask Optimus to render with NVIDIA; an explicit Windows app preference takes precedence.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
#endif
