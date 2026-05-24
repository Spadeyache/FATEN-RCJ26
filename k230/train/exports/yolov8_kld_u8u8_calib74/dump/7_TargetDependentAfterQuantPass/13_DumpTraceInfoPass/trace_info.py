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
DataItem(0, "TileLayerGroup_20", False, 0.0, 0.0, 0.0),
DataItem(1, "TileLayerGroup_19", False, 0.0, 0.0, 0.0),
DataItem(2, "TileLayerGroup_21", False, 0.0, 0.0, 0.0),
DataItem(3, "Concat", True, 0.0, 0.0, 0.0),
DataItem(4, "TileLayerGroup_18", False, 0.0, 0.0, 0.0),
DataItem(5, "TileLayerGroup_17", False, 0.0, 0.0, 0.0),
DataItem(6, "TileLayerGroup_22", False, 0.0, 0.0, 0.0),
DataItem(7, "Concat", True, 0.0, 0.0, 0.0),
DataItem(8, "TileLayerGroup_16", False, 0.0, 0.0, 0.0),
DataItem(9, "TileLayerGroup_15", False, 0.0, 0.0, 0.0),
DataItem(10, "TileLayerGroup_14", False, 0.0, 0.0, 0.0),
DataItem(11, "TileLayerGroup_23", False, 0.0, 0.0, 0.0),
DataItem(12, "Concat", True, 0.0, 0.0, 0.0),
DataItem(13, "TileLayerGroup_13", False, 0.0, 0.0, 0.0),
DataItem(14, "TileLayerGroup_12", False, 0.0, 0.0, 0.0),
DataItem(15, "TileLayerGroup_11", False, 0.0, 0.0, 0.0),
DataItem(16, "TileLayerGroup_24", False, 0.0, 0.0, 0.0),
DataItem(17, "Concat", True, 0.0, 0.0, 0.0),
DataItem(18, "TileLayerGroup_10", False, 0.0, 0.0, 0.0),
DataItem(19, "TileLayerGroup_25", False, 0.0, 0.0, 0.0),
DataItem(20, "TileLayerGroup_26", False, 0.0, 0.0, 0.0),
DataItem(21, "TileLayerGroup_27", False, 0.0, 0.0, 0.0),
DataItem(22, "TileLayerGroup_9", False, 0.0, 0.0, 0.0),
DataItem(23, "TileAct1_1", False, 0.0, 0.0, 0.0),
DataItem(24, "Concat", True, 0.0, 0.0, 0.0),
DataItem(25, "TileLayerGroup_8", False, 0.0, 0.0, 0.0),
DataItem(26, "TileLayerGroup_28", False, 0.0, 0.0, 0.0),
DataItem(27, "Concat", True, 0.0, 0.0, 0.0),
DataItem(28, "TileLayerGroup_7", False, 0.0, 0.0, 0.0),
DataItem(29, "TileAct1_0", False, 0.0, 0.0, 0.0),
DataItem(30, "Concat", True, 0.0, 0.0, 0.0),
DataItem(31, "TileLayerGroup_6", False, 0.0, 0.0, 0.0),
DataItem(32, "TileLayerGroup_29", False, 0.0, 0.0, 0.0),
DataItem(33, "Concat", True, 0.0, 0.0, 0.0),
DataItem(34, "TileLayerGroup_5", False, 0.0, 0.0, 0.0),
DataItem(35, "TileLayerGroup_4", False, 0.0, 0.0, 0.0),
DataItem(36, "TileLayerGroup_30", False, 0.0, 0.0, 0.0),
DataItem(37, "TileConcat_2", False, 0.0, 0.0, 0.0),
DataItem(38, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(39, "TileLayerGroup_34", False, 0.0, 0.0, 0.0),
DataItem(40, "Concat", True, 0.0, 0.0, 0.0),
DataItem(41, "TileLayerGroup_33", False, 0.0, 0.0, 0.0),
DataItem(42, "TileLayerGroup_35", False, 0.0, 0.0, 0.0),
DataItem(43, "Concat", True, 0.0, 0.0, 0.0),
DataItem(44, "TileLayerGroup_32", False, 0.0, 0.0, 0.0),
DataItem(45, "TileLayerGroup_31", False, 0.0, 0.0, 0.0),
DataItem(46, "TileLayerGroup_36", False, 0.0, 0.0, 0.0),
DataItem(47, "TileConcat_3", False, 0.0, 0.0, 0.0),
DataItem(48, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(49, "TileLayerGroup_40", False, 0.0, 0.0, 0.0),
DataItem(50, "Concat", True, 0.0, 0.0, 0.0),
DataItem(51, "TileLayerGroup_39", False, 0.0, 0.0, 0.0),
DataItem(52, "TileLayerGroup_41", False, 0.0, 0.0, 0.0),
DataItem(53, "Concat", True, 0.0, 0.0, 0.0),
DataItem(54, "TileLayerGroup_38", False, 0.0, 0.0, 0.0),
DataItem(55, "TileLayerGroup_37", False, 0.0, 0.0, 0.0),
DataItem(56, "TileLayerGroup_42", False, 0.0, 0.0, 0.0),
DataItem(57, "TileConcat_4", False, 0.0, 0.0, 0.0),
DataItem(58, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(59, "TileConcat_1", False, 0.0, 0.0, 0.0),
DataItem(60, "Squeeze", True, 0.0, 0.0, 0.0),
DataItem(61, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(62, "TileSplit_0", False, 0.0, 0.0, 0.0),
DataItem(63, "GetItem", True, 0.0, 0.0, 0.0),
DataItem(64, "Squeeze", True, 0.0, 0.0, 0.0),
DataItem(65, "GetItem", True, 0.0, 0.0, 0.0),
DataItem(66, "Squeeze", True, 0.0, 0.0, 0.0),
DataItem(67, "GetItem", True, 0.0, 0.0, 0.0),
DataItem(68, "TileLayerGroup_3", False, 0.0, 0.0, 0.0),
DataItem(69, "Softmax", True, 0.0, 0.0, 0.0),
DataItem(70, "TileLayerGroup_2", False, 0.0, 0.0, 0.0),
DataItem(71, "Slice", True, 0.0, 0.0, 0.0),
DataItem(72, "Binary", True, 0.0, 0.0, 0.0),
DataItem(73, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(74, "Slice", True, 0.0, 0.0, 0.0),
DataItem(75, "TileLayerGroup_43", False, 0.0, 0.0, 0.0),
DataItem(76, "TileLayerGroup_1", False, 0.0, 0.0, 0.0),
DataItem(77, "Reshape", True, 0.0, 0.0, 0.0),
DataItem(78, "Binary", True, 0.0, 0.0, 0.0),
DataItem(79, "TileLayerGroup_44", False, 0.0, 0.0, 0.0),
DataItem(80, "Concat", True, 0.0, 0.0, 0.0),
DataItem(81, "TileLayerGroup_0", False, 0.0, 0.0, 0.0),
DataItem(82, "GetItem", True, 0.0, 0.0, 0.0),
DataItem(83, "TileLayerGroup_45", False, 0.0, 0.0, 0.0),
DataItem(84, "TileConcat_0", False, 0.0, 0.0, 0.0),
DataItem(85, "Squeeze", True, 0.0, 0.0, 0.0),
])
if __name__ == '__main__':
  v0.print()
