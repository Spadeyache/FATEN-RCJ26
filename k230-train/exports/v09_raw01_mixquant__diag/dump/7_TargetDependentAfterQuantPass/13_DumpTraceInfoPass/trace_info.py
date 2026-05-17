from typing import List, Tuple

class DataItem:
    number: int
    name: int
    exec_time: float
    type: bool
    begin: float
    end: float
    def __init__(self, number, name, type, begin, end, exec_time) -> None:
        self.number = number
        self.name = name
        self.type = type
        self.begin = begin
        self.end = end
        self.exec_time = exec_time

class VersionData:
  name: str
  data: DataItem

  def __init__(self, name, data) -> None:
    self.name = name
    self.data = data

  def print(self):
    print(f'| number | name | type | begin | end | exec |')
    print(f'|-|-|-|-|-|-|')
    time = 0
    for p in self.data:
      print(f'| {p.number} | {p.name} | {p.type} | {p.begin} | {p.end} |  {p.exec_time} |')
      time += p.exec_time
    print(f'| total |  | {time} | | |')
v0 = VersionData("v0",[
DataItem(0, "TileLayerGroup_79", False, 0.0, 0.0, 0.0),
DataItem(1, "TileLayerGroup_78", False, 0.0, 0.0, 0.0),
DataItem(2, "TileLayerGroup_77", False, 0.0, 0.0, 0.0),
DataItem(3, "TileLayerGroup_76", False, 0.0, 0.0, 0.0),
DataItem(4, "TileLayerGroup_75", False, 0.0, 0.0, 0.0),
DataItem(5, "TileLayerGroup_74", False, 0.0, 0.0, 0.0),
DataItem(6, "TileLayerGroup_73", False, 0.0, 0.0, 0.0),
DataItem(7, "TileLayerGroup_72", False, 0.0, 0.0, 0.0),
DataItem(8, "TileLayerGroup_71", False, 0.0, 0.0, 0.0),
DataItem(9, "TileLayerGroup_70", False, 0.0, 0.0, 0.0),
DataItem(10, "TileLayerGroup_69", False, 0.0, 0.0, 0.0),
DataItem(11, "TileLayerGroup_68", False, 0.0, 0.0, 0.0),
DataItem(12, "TileLayerGroup_67", False, 0.0, 0.0, 0.0),
DataItem(13, "TileLayerGroup_66", False, 0.0, 0.0, 0.0),
DataItem(14, "TileLayerGroup_65", False, 0.0, 0.0, 0.0),
DataItem(15, "TileLayerGroup_64", False, 0.0, 0.0, 0.0),
DataItem(16, "TileAct1_12", False, 0.0, 0.0, 0.0),
DataItem(17, "TileConv2d_4", False, 0.0, 0.0, 0.0),
DataItem(18, "Concat", True, 0.0, 0.0, 0.0),
DataItem(19, "TileLayerGroup_63", False, 0.0, 0.0, 0.0),
DataItem(20, "TileLayerGroup_80", False, 0.0, 0.0, 0.0),
DataItem(21, "Concat", True, 0.0, 0.0, 0.0),
DataItem(22, "TileLayerGroup_62", False, 0.0, 0.0, 0.0),
DataItem(23, "TileAct1_11", False, 0.0, 0.0, 0.0),
DataItem(24, "TileConv2d_5", False, 0.0, 0.0, 0.0),
DataItem(25, "Concat", True, 0.0, 0.0, 0.0),
DataItem(26, "TileLayerGroup_61", False, 0.0, 0.0, 0.0),
DataItem(27, "TileLayerGroup_81", False, 0.0, 0.0, 0.0),
DataItem(28, "Concat", True, 0.0, 0.0, 0.0),
DataItem(29, "TileLayerGroup_60", False, 0.0, 0.0, 0.0),
DataItem(30, "TileLayerGroup_59", False, 0.0, 0.0, 0.0),
DataItem(31, "TileLayerGroup_85", False, 0.0, 0.0, 0.0),
DataItem(32, "Concat", True, 0.0, 0.0, 0.0),
DataItem(33, "TileLayerGroup_84", False, 0.0, 0.0, 0.0),
DataItem(34, "TileLayerGroup_86", False, 0.0, 0.0, 0.0),
DataItem(35, "Concat", True, 0.0, 0.0, 0.0),
DataItem(36, "TileLayerGroup_83", False, 0.0, 0.0, 0.0),
DataItem(37, "TileLayerGroup_82", False, 0.0, 0.0, 0.0),
DataItem(38, "TileLayerGroup_89", False, 0.0, 0.0, 0.0),
DataItem(39, "Concat", True, 0.0, 0.0, 0.0),
DataItem(40, "TileLayerGroup_88", False, 0.0, 0.0, 0.0),
DataItem(41, "TileLayerGroup_90", False, 0.0, 0.0, 0.0),
DataItem(42, "Concat", True, 0.0, 0.0, 0.0),
DataItem(43, "TileLayerGroup_87", False, 0.0, 0.0, 0.0),
])
if __name__ == '__main__':
  v0.print()
