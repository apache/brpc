"""
message MongoGetMoreRequest {
  required string database = 1;
  required string collection = 2;
  optional BsonPtr query = 3;
  optional int64_t skip = 4;
  optional int64_t limit = 5;
}
"""

import os

MESSAGE_NAME = "MongoCountRequest"
MESSAGE_FIELDS = (("required", "std::string", "database"),
                  ("required", "std::string", "collection"),
                  ("optional", "BsonPtr", "query"),
                  ("optional", "int64_t", "skip"),
                  ("optional", "int64_t", "limit"))

TRIVAL_TYPE = ("int64_t", "uint64_t", "int32_t", "uint32_t")

def is_trival_type(field_type):
  if field_type in set(TRIVAL_TYPE):
    return True
  else:
    return False

def message_declare_generate(message_name, fields):
  field_code = ""
  """
  0 -> MessageName
  1 -> field code
  """
  message_declare = """
class {0} : public ::google::protobuf::Message {{
 public:
   {0}();
   virtual ~{0}();
   {0}(const {0}& from);
   {0}& operator=(const {0}& from);
   void Swap({0}* other);
   bool SerializeTo(butil::IOBuf* buf) const;
   {0}* New() const;
   void CopyFrom(const ::google::protobuf::Message& from);
   void MergeFrom(const ::google::protobuf::Message& from);
   void CopyFrom(const {0}& from);
   void MergeFrom(const {0}& from);
   void Clear();
   bool IsInitialized() const;
   bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
   void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
   ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
   int GetCachedSize() const {{ return _cached_size_; }}
   static const ::google::protobuf::Descriptor* descriptor();

   // fields
   {1}

  protected:
   ::google::protobuf::Metadata GetMetadata() const override;

  private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    ::google::protobuf::internal::HasBits<1> _has_bits_;
    mutable int _cached_size_;
}};
"""
  for i in range(len(MESSAGE_FIELDS)):
    field_code = field_code + field_generate(MESSAGE_NAME, MESSAGE_FIELDS[i], i + 1)
  return message_declare.format(message_name, field_code)

def sharedctor_generate(fields):
  result = "_cached_size_ = 0;\n"
  for field in fields:
    message_field_type = field[0]
    field_type = field[1]
    field_name = field[2]
    if not message_field_type == "repeated" and is_trival_type(field_type):
      result = result + "{0}_ = 0;\n".format(field_name)
  return result

def mergefrom_generate(fields):
  result = ""
  for field in fields:
    message_field_type = field[0]
    field_type = field[1]
    field_name = field[2]
    if not message_field_type == "repeated":
      result = result + """
      if (from.has_{0}()) {{
        set_{0}(from.{0}());
      }}
      """.format(field_name)
    else:
      # repated
      result = result + """
      {0}_.insert({0}_.end(), from.{0}.cbegin(), from.{0}.cend());
      """.format(field_name)
  return result

def clear_generate(fields):
  result = ""
  for field in fields:
    field_name = field[2]
    result += "clear_{0}();\n".format(field_name)
  return result

def isinit_generate(fields):
  result = ""
  for field in fields:
    message_field_type = field[0]
    field_name = field[2]
    if message_field_type == "required":
      if len(result) == 0:
        result += "return has_{0}()".format(field_name)
      else:
        result += "  && has_{0}()".format(field_name)
  if len(result) == 0:
    result = "return true;"
  else:
    result += ";"
  return result

def message_define_generate(message_name, fields):
  """
  0 -> MessageName
  1 -> SharedCtorBody
  2 -> MergeFromBody
  3 -> ClearBody
  4 -> IsInitBody
  """
  message_define = """
{0}::{0}() : ::google::protobuf::Message() {{
  SharedCtor();
}}

{0}::~{0}() {{ SharedDtor(); }}

{0}::{0}(const {0}& from)
    : ::google::protobuf::Message() {{
  SharedCtor();
  MergeFrom(from);
}}

{0}& {0}::operator=(const {0}& from) {{
  CopyFrom(from);
  return *this;
}}

void {0}::SharedCtor() {{
  {1}
}}

void {0}::SharedDtor() {{}}

bool {0}::SerializeTo(butil::IOBuf* buf) const {{
  // TODO custom definetion
}}

void {0}::Swap({0}* other) {{}}

{0}* {0}::New() const {{
  return new {0}();
}}

void {0}::CopyFrom(const ::google::protobuf::Message& from) {{
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}}

void {0}::MergeFrom(const ::google::protobuf::Message& from) {{
  GOOGLE_CHECK_NE(&from, this);
  const {0}* source =
      dynamic_cast<const {0}*>(&from);
  if (source == NULL) {{
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  }} else {{
    MergeFrom(*source);
  }}
}}

void {0}::CopyFrom(const {0}& from) {{
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}}

void {0}::MergeFrom(const {0}& from) {{
  GOOGLE_CHECK_NE(&from, this);
  {2}
}}

void {0}::Clear() {{
  {3}
}}

bool {0}::IsInitialized() const {{
  {4}
}}

bool {0}::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {{
  LOG(WARNING) << "You're not supposed to parse a {0}";
  return true;
}}

void {0}::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {{
  LOG(WARNING) << "You're not supposed to serialize a {0}";
}}

::google::protobuf::uint8* {0}::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {{
  return output;
}}

const ::google::protobuf::Descriptor* {0}::descriptor() {{
  return {0}Base::descriptor();
}}

::google::protobuf::Metadata {0}::GetMetadata() const {{
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}}

void {0}::SetCachedSize(int size) const {{
  _cached_size_ = size;
}}
"""
  return message_define.format(message_name, sharedctor_generate(fields), 
  mergefrom_generate(fields), clear_generate(fields), isinit_generate(fields))


"""
INPUT: tuple: ("required", "string", "database")
       int: filed_number
OUTPUT: tuple: (func_declare_1, func_declare_2, field_define, func_define)
"""
def field_generate(message_name, field_info, filed_number):
  message_field_type = field_info[0]  # optional, required, repeated
  field_type = field_info[1]  # string, BsonPtr, int32_t
  field_name = field_info[2]  # query, limit, skip
  """
  0 -> field_name
  1 -> type
  2 -> field_number
  3 -> has_bit(0x00000001u)
  """
  trival_declare_1 = """
  // {0}
 public:
  static const int k{0}FieldNumber = {2};
  {1} {0}() const {{
    return {0}_;
  }}
  bool has_{0}() const {{
    return  _has_bits_[0] & {3};
  }}
  void clear_{0}() {{
    clear_has_{0}();
    {0}_ = 0;
  }}
  void set_{0}({1} value) {{
    {0}_ = value;
    set_has_{0}();
  }}
 private:
  void set_has_{0}() {{
    _has_bits_[0] |= {3};
  }}
  void clear_has_{0}() {{
    _has_bits_[0] &= ~{3};
  }}

  {1} {0}_;
  """

  """
  0 -> field_name
  1 -> type
  2 -> field_number
  3 -> has_bit(0x00000001u)
  """
  notrival_declare_1 = """
  // {0}
 public:
  static const int k{0}FieldNumber = {2};
  const {1}& {0}() const {{
    return {0}_;
  }}
  bool has_{0}() const {{
    return  _has_bits_[0] & {3};
  }}
  void clear_{0}() {{
    clear_has_{0}();
    {0}_.clear();
  }}
  void set_{0}({1} value) {{
    {0}_ = value;
    set_has_{0}();
  }}
 private:
  void set_has_{0}() {{
    _has_bits_[0] |= {3};
  }}
  void clear_has_{0}() {{
    _has_bits_[0] &= ~{3};
  }}

  {1} {0}_;
  """

  """
  0 -> field_name
  1 -> type
  2 -> field_number
  """
  repeated_declare_1 = """
  // {0}
 public:
  static const int k{0}FieldNumber = {2};
  const std::vector<{1}>& {0}() const {{
    return {0}_;
  }}
  int {0}_size() const {{
    return {0}_.size();
  }}
  void clear_{0}() {{
    {0}_.clear();
  }}
  const {1}& {0}(int index) const {{
    return {0}_[index];
  }}
  {1}* mutable_{0}(int index) {{
    return &{0}_[index];
  }}
  void add_{0}({1} value) {{
    {0}_.push_back(std::move(value));
  }}

 private:
  std::vector<{1}> {0}_;
  """
  is_trival = is_trival_type(field_type)
  bit_str = hex(pow(2, filed_number - 1)) + "u"
  if message_field_type == "repeated":
    return repeated_declare_1.format(field_name, field_type, filed_number)
  elif is_trival:
    return trival_declare_1.format(field_name, field_type, filed_number, bit_str)
  else:
    return notrival_declare_1.format(field_name, field_type, filed_number, bit_str)

if __name__ == "__main__":
  with open(MESSAGE_NAME + ".h", 'w') as head_file:
    head_file.seek(0)
    head_file.truncate()
    head_file.write(message_declare_generate(MESSAGE_NAME, MESSAGE_FIELDS))
  with open(MESSAGE_NAME + ".cpp", 'w') as source_file:
    source_file.seek(0)
    source_file.truncate()
    source_file.write(message_define_generate(MESSAGE_NAME, MESSAGE_FIELDS))
  os.system("clang-format -style=Google -i %s.*" % (MESSAGE_NAME,))
  # source_file = open(MESSAGE_NAME + ".cpp")
  # print(message_define_generate(MESSAGE_NAME, MESSAGE_FIELDS))
  # print(message_declare_generate(MESSAGE_NAME, MESSAGE_FIELDS))
