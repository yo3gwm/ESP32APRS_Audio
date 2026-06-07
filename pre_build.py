Import("env")
from datetime import datetime

ts = datetime.now().strftime("%Y%m%d_%H%M")

with open("include/build_timestamp.h", "w") as f:
    f.write(f'#pragma once\n#define BUILD_TIMESTAMP "{ts}"\n')
