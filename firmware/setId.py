
Import("env")

print(env.Dump())

board_config = env.BoardConfig()
# should be array of VID:PID pairs
board_config.update("build.hwids", [
  ["0x4545", "0x4545"] # VID=0xffff, PID=0x1111の場合
])