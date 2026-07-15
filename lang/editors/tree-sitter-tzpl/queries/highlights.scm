; Keywords
[
  "fn"
  "let"
  "var"
  "const"
  "struct"
  "enum"
  "type"
  "constraint"
  "requires"
  "import"
  "as"
  "if"
  "else"
  "while"
  "for"
  "match"
  "return"
  "yield"
  "try"
  "async"
  "await"
  "break"
  "continue"
  "where"
  "coro"
  "private"
] @keyword

; Literals
(integer_literal) @number
(float_literal) @number.float
(imaginary_literal) @number
(string_literal) @string
(triple_string_literal) @string
(guillemet_string_literal) @string
(escape_sequence) @string.escape
(symbol_literal) @string.special.symbol
(boolean_literal) @constant.builtin
(nil_literal) @constant.builtin

; Built-in types
(primitive_type) @type.builtin

; Types
(type_identifier) @type
(generic_type name: (identifier) @type)
(struct_definition name: (identifier) @type.definition)
(enum_definition name: (identifier) @type.definition)
(type_alias name: (identifier) @type.definition)
(constraint_declaration name: (identifier) @type.definition)
(type_param name: (identifier) @type)

; Functions
(function_definition name: (identifier) @function.definition)
(function_definition name: (operator_name) @operator)
(call_expression
  function: (primary_expression
    (identifier) @function.call))
(lambda_expression "fn" @keyword.function)

; Variables
(let_declaration pattern: (identifier) @variable)
(var_declaration name: (identifier) @variable)
(const_declaration name: (identifier) @constant)
(parameter name: (identifier) @variable.parameter)

; Fields
(field_expression field: (identifier) @property)
(struct_field name: (identifier) @property)
(struct_field_value name: (identifier) @property)

; Enum cases
(enum_case name: (identifier) @constant)
(enum_pattern case: (identifier) @constant)
(enum_pattern type: (identifier) @type)

; Pattern matching
(wildcard_pattern) @variable.builtin
(binding_pattern (identifier) @variable)
(rest_pattern) @punctuation.special

; Struct/enum names in patterns
(struct_pattern type: (identifier) @type)

; Space pipeline function
(space_pipeline function: (identifier) @function.call)

; Struct construction type name
(struct_construction type: (identifier) @type)

; Special identifiers
(this_expression) @variable.builtin

; Operators
(automap_operator) @operator

; Delimiters
["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" ":" "."] @punctuation.delimiter
["?" "="] @operator

; Import paths
(import_path (identifier) @module)

; Comments
(line_comment) @comment
(block_comment) @comment
