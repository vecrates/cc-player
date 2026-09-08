import io, sys

path = r"D:\code\github\cc-player\core\src\player\PlayerImpl.cpp"
with io.open(path, "r", encoding="utf-8") as f:
    lines = f.readlines()

out = []
for line in lines:
    out.append(line)
    if "\u89e3\u7801\u7ebf\u7a0b\u4e0e AAudio \u56de\u8c03\u7ebf\u7a0b\u4e4b\u95f4\u7684\u65e0\u9501\u7f13\u51b2" in line:
        out.append("                    m_ringBuffer = new AudioRingBuffer();\n")
    elif line.strip().startswith("// 4.") and "\u4e32\u6210\u6d41\u6c34\u7ebf" in line:
        out.append("            m_demuxer.setPacketQueues(&m_videoPacketQueue, &m_audioPacketQueue);\n")

with io.open(path, "w", encoding="utf-8", newline="\n") as f:
    f.writelines(out)
print("done, total lines:", len(out))
