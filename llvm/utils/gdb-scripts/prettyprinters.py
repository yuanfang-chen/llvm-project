from __future__ import print_function
import sys

import gdb.printing
import gdb.types

class Iterator:
  def __iter__(self):
    return self

  if sys.version_info.major == 2:
      def next(self):
        return self.__next__()

  def children(self):
    return self

def escape_bytes(val, l):
  return '"' + val.string(encoding='Latin-1', length=l).encode('unicode_escape').decode() + '"'

class SmallStringPrinter:
  """Print an llvm::SmallString object."""

  def __init__(self, val):
    self.val = val

  def to_string(self):
    begin = self.val['BeginX']
    return escape_bytes(begin.cast(gdb.lookup_type('char').pointer()), self.val['Size'])

class StringRefPrinter:
  """Print an llvm::StringRef object."""

  def __init__(self, val):
    self.val = val

  def to_string(self):
    return escape_bytes(self.val['Data'], self.val['Length'])

class SmallVectorPrinter(Iterator):
  """Print an llvm::SmallVector object."""

  def __init__(self, val):
    self.val = val
    t = val.type.template_argument(0).pointer()
    self.begin = val['BeginX'].cast(t)
    self.size = val['Size']
    self.i = 0

  def __next__(self):
    if self.i == self.size:
      raise StopIteration
    ret = '[{}]'.format(self.i), (self.begin+self.i).dereference()
    self.i += 1
    return ret

  def to_string(self):
    return 'llvm::SmallVector of Size {}, Capacity {}'.format(self.size, self.val['Capacity'])

  def display_hint (self):
    return 'array'

class ArrayRefPrinter:
  """Print an llvm::ArrayRef object."""

  class _iterator:
    def __init__(self, begin, end):
      self.cur = begin
      self.end = end
      self.count = 0

    def __iter__(self):
      return self

    def __next__(self):
      if self.cur == self.end:
        raise StopIteration
      count = self.count
      self.count = self.count + 1
      cur = self.cur
      self.cur = self.cur + 1
      return '[%d]' % count, cur.dereference()

    if sys.version_info.major == 2:
        next = __next__

  def __init__(self, val):
    self.val = val

  def children(self):
    data = self.val['Data']
    return self._iterator(data, data + self.val['Length'])

  def to_string(self):
    return 'llvm::ArrayRef of length %d' % (self.val['Length'])

  def display_hint (self):
    return 'array'

class ExpectedPrinter(Iterator):
  """Print an llvm::Expected object."""

  def __init__(self, val):
    self.val = val

  def __next__(self):
    val = self.val
    if val is None:
      raise StopIteration
    self.val = None
    if val['HasError']:
      return ('error', val['ErrorStorage'].address.cast(
          gdb.lookup_type('llvm::ErrorInfoBase').pointer()).dereference())
    return ('value', val['TStorage'].address.cast(
        val.type.template_argument(0).pointer()).dereference())

  def to_string(self):
    return 'llvm::Expected{}'.format(' is error' if self.val['HasError'] else '')

class OptionalPrinter(Iterator):
  """Print an llvm::Optional object."""

  def __init__(self, val):
    self.val = val

  def __next__(self):
    val = self.val
    if val is None:
      raise StopIteration
    self.val = None
    if not val['Storage']['hasVal']:
      raise StopIteration
    return ('value', val['Storage']['value'])

  def to_string(self):
    return 'llvm::Optional{}'.format('' if self.val['Storage']['hasVal'] else ' is not initialized')

class DenseMapPrinter:
  "Print a DenseMap"

  class _iterator:
    def __init__(self, key_info_t, begin, end):
      self.key_info_t = key_info_t
      self.cur = begin
      self.end = end
      self.advancePastEmptyBuckets()
      self.first = True

    def __iter__(self):
      return self

    def advancePastEmptyBuckets(self):
      # disabled until the comments below can be addressed
      # keeping as notes/posterity/hints for future contributors
      return
      n = self.key_info_t.name
      is_equal = gdb.parse_and_eval(n + '::isEqual')
      empty = gdb.parse_and_eval(n + '::getEmptyKey()')
      tombstone = gdb.parse_and_eval(n + '::getTombstoneKey()')
      # the following is invalid, GDB fails with:
      #   Python Exception <class 'gdb.error'> Attempt to take address of value
      #   not located in memory.
      # because isEqual took parameter (for the unsigned long key I was testing)
      # by const ref, and GDB
      # It's also not entirely general - we should be accessing the "getFirst()"
      # member function, not the 'first' member variable, but I've yet to figure
      # out how to find/call member functions (especially (const) overloaded
      # ones) on a gdb.Value.
      while self.cur != self.end and (is_equal(self.cur.dereference()['first'], empty) or is_equal(self.cur.dereference()['first'], tombstone)):
        self.cur = self.cur + 1

    def __next__(self):
      if self.cur == self.end:
        raise StopIteration
      cur = self.cur
      v = cur.dereference()['first' if self.first else 'second']
      if not self.first:
        self.cur = self.cur + 1
        self.advancePastEmptyBuckets()
        self.first = True
      else:
        self.first = False
      return 'x', v

    if sys.version_info.major == 2:
        next = __next__

  def __init__(self, val):
    self.val = val

  def children(self):
    t = self.val.type.template_argument(3).pointer()
    begin = self.val['Buckets'].cast(t)
    end = (begin + self.val['NumBuckets']).cast(t)
    return self._iterator(self.val.type.template_argument(2), begin, end)

  def to_string(self):
    return 'llvm::DenseMap with %d elements' % (self.val['NumEntries'])

  def display_hint(self):
    return 'map'

class TwinePrinter:
  "Print a Twine"

  def __init__(self, val):
    self._val = val

  def display_hint(self):
    return 'string'

  def string_from_pretty_printer_lookup(self, val):
    '''Lookup the default pretty-printer for val and use it.

    If no pretty-printer is defined for the type of val, print an error and
    return a placeholder string.'''

    pp = gdb.default_visualizer(val)
    if pp:
      s = pp.to_string()

      # The pretty-printer may return a LazyString instead of an actual Python
      # string.  Convert it to a Python string.  However, GDB doesn't seem to
      # register the LazyString type, so we can't check
      # "type(s) == gdb.LazyString".
      if 'LazyString' in type(s).__name__:
        s = s.value().address.string()

    else:
      print(('No pretty printer for {} found. The resulting Twine ' +
             'representation will be incomplete.').format(val.type.name))
      s = '(missing {})'.format(val.type.name)

    return s

  def is_twine_kind(self, kind, expected):
    if not kind.endswith(expected):
      return False
    # apparently some GDB versions add the NodeKind:: namespace
    # (happens for me on GDB 7.11)
    return kind in ('llvm::Twine::' + expected,
                    'llvm::Twine::NodeKind::' + expected)

  def string_from_child(self, child, kind):
    '''Return the string representation of the Twine::Child child.'''

    if self.is_twine_kind(kind, 'EmptyKind') or self.is_twine_kind(kind, 'NullKind'):
      return ''

    if self.is_twine_kind(kind, 'TwineKind'):
      return self.string_from_twine_object(child['twine'].dereference())

    if self.is_twine_kind(kind, 'CStringKind'):
      return child['cString'].string()

    if self.is_twine_kind(kind, 'StdStringKind'):
      val = child['stdString'].dereference()
      return self.string_from_pretty_printer_lookup(val)

    if self.is_twine_kind(kind, 'StringRefKind'):
      val = child['stringRef'].dereference()
      pp = StringRefPrinter(val)
      return pp.to_string()

    if self.is_twine_kind(kind, 'SmallStringKind'):
      val = child['smallString'].dereference()
      pp = SmallStringPrinter(val)
      return pp.to_string()

    if self.is_twine_kind(kind, 'CharKind'):
      return chr(child['character'])

    if self.is_twine_kind(kind, 'DecUIKind'):
      return str(child['decUI'])

    if self.is_twine_kind(kind, 'DecIKind'):
      return str(child['decI'])

    if self.is_twine_kind(kind, 'DecULKind'):
      return str(child['decUL'].dereference())

    if self.is_twine_kind(kind, 'DecLKind'):
      return str(child['decL'].dereference())

    if self.is_twine_kind(kind, 'DecULLKind'):
      return str(child['decULL'].dereference())

    if self.is_twine_kind(kind, 'DecLLKind'):
      return str(child['decLL'].dereference())

    if self.is_twine_kind(kind, 'UHexKind'):
      val = child['uHex'].dereference()
      return hex(int(val))

    print(('Unhandled NodeKind {} in Twine pretty-printer. The result will be '
           'incomplete.').format(kind))

    return '(unhandled {})'.format(kind)

  def string_from_twine_object(self, twine):
    '''Return the string representation of the Twine object twine.'''

    lhs_str = ''
    rhs_str = ''

    lhs = twine['LHS']
    rhs = twine['RHS']
    lhs_kind = str(twine['LHSKind'])
    rhs_kind = str(twine['RHSKind'])

    lhs_str = self.string_from_child(lhs, lhs_kind)
    rhs_str = self.string_from_child(rhs, rhs_kind)

    return lhs_str + rhs_str

  def to_string(self):
    return self.string_from_twine_object(self._val)

def make_printer(string = None, children = None, hint = None):
  """Create a printer from the parameters."""
  class Printer : pass
  printer = Printer()
  if string:
    setattr(printer, 'to_string', lambda: string)
  if children:
    setattr(printer, 'children', lambda: children)
  if hint:
    setattr(printer, 'display_hint', lambda: hint)
  return printer

def get_pointer_int_pair(val):
  """Get tuple from llvm::PointerIntPair."""
  info_name = val.type.template_argument(4).strip_typedefs().name
  try:
    enum_type = gdb.lookup_type(info_name + '::MaskAndShiftConstants')
  except gdb.error:
    return (None, None)
  enum_dict = gdb.types.make_enum_dict(enum_type)
  ptr_mask = enum_dict[info_name + '::PointerBitMask']
  int_shift = enum_dict[info_name + '::IntShift']
  int_mask = enum_dict[info_name + '::IntMask']
  pair_union = val['Value']
  pointer = (pair_union & ptr_mask)
  value = ((pair_union >> int_shift) & int_mask)
  return (pointer, value)

def make_pointer_int_pair_printer(val):
  """Factory for an llvm::PointerIntPair printer."""
  pointer, value = get_pointer_int_pair(val)
  if not pointer or not value:
    return None
  pointer_type = val.type.template_argument(0)
  value_type = val.type.template_argument(2)
  string = 'llvm::PointerIntPair<%s>' % pointer_type
  children = [('pointer', pointer.cast(pointer_type)),
              ('value', value.cast(value_type))]
  return make_printer(string, children)

def make_pointer_union_printer(val):
  """Factory for an llvm::PointerUnion printer."""
  pointer, value = get_pointer_int_pair(val['Val'])
  if not pointer or not value:
    return None
  pointer_type = val.type.template_argument(int(value))
  string = 'llvm::PointerUnion containing %s' % pointer_type
  return make_printer(string, [('pointer', pointer.cast(pointer_type))])

pp = gdb.printing.RegexpCollectionPrettyPrinter("LLVMSupport")
pp.add_printer('llvm::SmallString', '^llvm::SmallString<.*>$', SmallStringPrinter)
pp.add_printer('llvm::StringRef', '^llvm::StringRef$', StringRefPrinter)
pp.add_printer('llvm::SmallVectorImpl', '^llvm::SmallVector(Impl)?<.*>$', SmallVectorPrinter)
pp.add_printer('llvm::ArrayRef', '^llvm::(Mutable)?ArrayRef<.*>$', ArrayRefPrinter)
pp.add_printer('llvm::Expected', '^llvm::Expected<.*>$', ExpectedPrinter)
pp.add_printer('llvm::Optional', '^llvm::Optional<.*>$', OptionalPrinter)
pp.add_printer('llvm::DenseMap', '^llvm::DenseMap<.*>$', DenseMapPrinter)
pp.add_printer('llvm::Twine', '^llvm::Twine$', TwinePrinter)
pp.add_printer('llvm::PointerIntPair', '^llvm::PointerIntPair<.*>$', make_pointer_int_pair_printer)
pp.add_printer('llvm::PointerUnion', '^llvm::PointerUnion<.*>$', make_pointer_union_printer)
gdb.printing.register_pretty_printer(gdb.current_objfile(), pp)
