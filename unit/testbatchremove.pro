include("pre.pri")

# win32 cross-compile generates git.h in _win32/, not _build/
win32 { INCLUDEPATH += ../_win32 }

FILES += $$FILES_INDEX colordescindex dctfeaturesindex cvfeaturesindex

include("post.pri")
