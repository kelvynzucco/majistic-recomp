using namespace System
using namespace System.IO
using namespace System.Text
using namespace System.Collections.Generic

param(
  [string]$IsoPath,
  [string]$OutputDir
)

$csharp = @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

namespace IsoDumper {
  public class Extractor {
    sealed class Entry { public string Path; public long Offset; public int Size; }
    public static void Extract(string isoPath, string outDir) {
      using(FileStream stream = new FileStream(isoPath, FileMode.Open, FileAccess.Read, FileShare.Read)) {
        long partition = -1;
        byte[] descriptor = null;
        foreach(long candidate in new long[]{0, 0xFB20, 0x20600, 0x2080000, 0xFD90000, 0x18300000}) {
          long pos = candidate + 65536;
          if(pos + 2048 > stream.Length) continue;
          stream.Position = pos;
          byte[] d = new byte[2048];
          stream.Read(d, 0, 2048);
          if(Encoding.ASCII.GetString(d, 0, 20) == "MICROSOFT*XBOX*MEDIA" && Encoding.ASCII.GetString(d, 2028, 20) == "MICROSOFT*XBOX*MEDIA") {
            descriptor = d; partition = candidate; break;
          }
        }
        if(descriptor == null) throw new Exception("No Xbox partition found");
        Console.WriteLine("Partition found at: 0x" + partition.ToString("X"));
        
        List<Entry> files = new List<Entry>();
        uint rootSector = BitConverter.ToUInt32(descriptor, 20);
        uint rootSize = BitConverter.ToUInt32(descriptor, 24);

        Stack<Tuple<uint, uint, string>> dirs = new Stack<Tuple<uint, uint, string>>();
        dirs.Push(new Tuple<uint, uint, string>(rootSector, rootSize, ""));

        while(dirs.Count > 0) {
          var item = dirs.Pop();
          uint sector = item.Item1;
          uint size = item.Item2;
          string parent = item.Item3;
          if(size == 0) continue;
          byte[] table = new byte[size];
          stream.Position = partition + (long)sector * 2048;
          stream.Read(table, 0, (int)size);
          Stack<int> pending = new Stack<int>();
          pending.Push(0);
          HashSet<int> visited = new HashSet<int>();

          while(pending.Count > 0) {
            int offset = pending.Pop();
            while(offset >= 0 && offset + 14 <= size && visited.Add(offset)) {
              ushort lSub = BitConverter.ToUInt16(table, offset);
              ushort rSub = BitConverter.ToUInt16(table, offset + 2);
              uint fSector = BitConverter.ToUInt32(table, offset + 4);
              uint fSize = BitConverter.ToUInt32(table, offset + 8);
              byte attr = table[offset + 12];
              byte nameLen = table[offset + 13];
              if(offset + 14 + nameLen > size) break;
              string name = Encoding.ASCII.GetString(table, offset + 14, nameLen);
              string rel = string.IsNullOrEmpty(parent) ? name : parent + "\\" + name;

              if((attr & 0x10) != 0) {
                dirs.Push(new Tuple<uint, uint, string>(fSector, fSize, rel));
              } else {
                files.Add(new Entry { Path = rel, Offset = partition + (long)fSector * 2048, Size = (int)fSize });
              }

              if(rSub != 0 && rSub != 0xFFFF) pending.Push(rSub * 4);
              if(lSub != 0 && lSub != 0xFFFF) offset = lSub * 4;
              else break;
            }
          }
        }

        Console.WriteLine("Total files found: " + files.Count);
        int count = 0;
        byte[] buffer = new byte[1048576];
        foreach(var f in files) {
          string target = System.IO.Path.Combine(outDir, f.Path);
          Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target));
          stream.Position = f.Offset;
          using(FileStream outStream = new FileStream(target, FileMode.Create, FileAccess.Write)) {
            int remaining = f.Size;
            while(remaining > 0) {
              int toRead = Math.Min(remaining, buffer.Length);
              int read = stream.Read(buffer, 0, toRead);
              if(read <= 0) break;
              outStream.Write(buffer, 0, read);
              remaining -= read;
            }
          }
          count++;
          if(count % 500 == 0 || count == files.Count) {
            Console.WriteLine(string.Format("Extracted {0}/{1} files...", count, files.Count));
          }
        }
        Console.WriteLine("Extraction complete!");
      }
    }
  }
}
'@

Add-Type -TypeDefinition $csharp

if (-not $IsoPath -or -not $OutputDir) {
  Write-Host "Usage: .\extract-iso.ps1 -IsoPath <path-to-iso> -OutputDir <destination-folder>"
  exit 1
}

Write-Host "Extracting Xbox 360 ISO: $IsoPath"
Write-Host "Destination: $OutputDir"
[IsoDumper.Extractor]::Extract($IsoPath, $OutputDir)
