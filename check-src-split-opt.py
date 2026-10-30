# check_code v1.0
# run from root of repository
import subprocess

_qt_autogen_macro_list = (
    'signals:',
    'Q_CLASSINFO',
    'Q_PLUGIN_METADATA',
    'Q_INTERFACES',
    'Q_PROPERTY',
    'Q_PRIVATE_PROPERTY',
    'Q_REVISION',
    'Q_OVERRIDE',
    'Q_ENUM',
    'Q_FLAGS',
    'Q_SCRIPTABLE',
    'Q_INVOKABLE',
    'Q_SIGNAL',
    'Q_SLOT'
)
_qt_autogen_macro_regexp = '({0})'.format('|'.join(_qt_autogen_macro_list))

CPP_EXTENSIONS = ('.h', '.hpp', '.hxx', '.cpp', '.cxx', '.cc')

def _FnamesToBaseFileNames(fnames_list):
    result = []
    for fn in fnames_list:
        # Ignore markdown, cmake, dotfiles, and non-C++ files
        if not fn.lower().endswith(CPP_EXTENSIONS):
            continue
        start = fn.rfind('/')
        end = fn.rfind('.')
        if end > start:
            base = fn[start + 1 : end]
            if base:
                result.append(base)
    return result

def CheckQObject():
    def get_files(pattern):
        try:
            output = subprocess.check_output(
                ['git', 'grep', '-E', pattern], 
                text=True
            )
        except subprocess.CalledProcessError:
            # git grep exits with 1 if no matches are found
            return set()
        
        files = output.splitlines()
        files = [s[:s.find(':')] for s in files if ':' in s]
        return set(files)

    qobject_files = _FnamesToBaseFileNames(get_files('Q_OBJECT'))
    macro_files = set(_FnamesToBaseFileNames(get_files(_qt_autogen_macro_regexp)))
    
    # [^"]+ is standard POSIX ERE and avoids the repetition operator error
    include_moc_files = set(_FnamesToBaseFileNames(get_files(r'#include "moc_[^"]+\.cpp"')))

    for fn in qobject_files:
        if fn not in include_moc_files:
            print('looks like {0} misses include for moc_* file'.format(fn))
        if fn not in macro_files:
            print('probably {0} has redundant Q_OBJECT macro'.format(fn))

if __name__ == '__main__':
    CheckQObject()
