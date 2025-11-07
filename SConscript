from building import *

cwd     = GetCurrentDir()
src     = Glob('*.c')
CPPPATH = [cwd]

group = DefineGroup('RT_INSIGHT', src, depend = [], CPPPATH = CPPPATH)

Return('group')
