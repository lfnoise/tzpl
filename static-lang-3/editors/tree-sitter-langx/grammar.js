/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  TERNARY: 0,     // ? :
  ARROW: 1,       // <- ->
  PIPELINE: 2,    // |>
  OR: 3,          // ||
  AND: 4,         // &&
  CONS: 5,        // ::
  EQUAL: 6,       // == !=
  COMPARE: 7,     // < <= > >=
  BIT_OR: 8,      // |
  BIT_XOR: 9,     // ^
  BIT_AND: 10,    // &
  SHIFT: 11,      // << >> >>>
  ADD: 12,        // + - $
  MUL: 13,        // * / % //
  UNARY: 14,      // - ! ~ & *
  POSTFIX: 15,    // call, index, field, automap
};

module.exports = grammar({
  name: 'langx',

  externals: $ => [
    $.block_comment,
  ],

  extras: $ => [
    /\s/,
    $.line_comment,
    $.block_comment,
  ],

  word: $ => $.identifier,

  inline: $ => [$._top_level, $._literal],

  conflicts: $ => [
    // Identifier as binding_pattern vs type_identifier in struct patterns
    [$.binding_pattern, $.type_identifier],
  ],

  rules: {
    source_file: $ => repeat($._top_level),

    _top_level: $ => choice(
      $._declaration,
      $._statement,
    ),

    // ===================== DECLARATIONS =====================

    _declaration: $ => choice(
      $.function_definition,
      $.let_declaration,
      $.var_declaration,
      $.const_declaration,
      $.struct_definition,
      $.enum_definition,
      $.type_alias,
      $.constraint_declaration,
      $.import_declaration,
    ),

    function_definition: $ => seq(
      optional('private'),
      optional('coro'),
      'fn',
      field('name', choice($.identifier, $.operator_name)),
      optional($.type_parameters),
      $.parameter_list,
      optional(field('return_type', $._type)),
      optional($.where_clause),
      choice(
        seq('=', field('body', $._expression), ';'),
        field('body', $.block),
      ),
    ),

    operator_name: $ => choice(
      '+', '-', '*', '/', '%',
      token(seq('/', '/')),   // //
      '==', '!=', '<', '<=', '>', '>=',
      '&&', '||',
      '&', '|', '^', '~',
      '<<', '>>', '>>>',
      '::', '$',
      '<-', '->',
    ),

    type_parameters: $ => seq(
      '<',
      commaSep1($.type_param),
      '>',
    ),

    type_param: $ => seq(
      field('name', $.identifier),
      optional(seq(':', sep1($._type, '&'))),
    ),

    parameter_list: $ => seq(
      '(',
      commaSep($.parameter),
      ')',
    ),

    parameter: $ => seq(
      optional('...'),
      field('name', $.identifier),
      optional(field('type', $._type)),
      optional(seq('=', field('default', $._expression))),
    ),

    where_clause: $ => seq(
      'where',
      commaSep1($.type_constraint),
    ),

    type_constraint: $ => seq(
      $.identifier,
      ':',
      sep1($._type, '&'),
    ),

    let_declaration: $ => seq(
      'let',
      field('pattern', choice(
        seq($.identifier, optional(field('type', $._type))),
        $.tuple_pattern,
        $.array_pattern,
        $.struct_pattern,
      )),
      '=',
      field('value', $._expression),
      ';',
    ),

    var_declaration: $ => seq(
      'var',
      field('name', $.identifier),
      optional(field('type', $._type)),
      '=',
      field('value', $._expression),
      ';',
    ),

    const_declaration: $ => seq(
      'const',
      field('name', $.identifier),
      optional(field('type', $._type)),
      '=',
      field('value', $._expression),
      ';',
    ),

    struct_definition: $ => seq(
      optional('private'),
      'struct',
      field('name', $.identifier),
      optional($.type_parameters),
      choice(
        $.struct_body,
        seq('(', commaSep($._type), ')', ';'),
      ),
    ),

    struct_body: $ => seq(
      '{',
      commaSep($.struct_field),
      '}',
    ),

    struct_field: $ => seq(
      field('name', $.identifier),
      field('type', $._type),
    ),

    enum_definition: $ => seq(
      optional('private'),
      'enum',
      field('name', $.identifier),
      optional($.type_parameters),
      '{',
      commaSep1($.enum_case),
      '}',
    ),

    enum_case: $ => seq(
      field('name', $.identifier),
      optional(field('type', $._type)),
    ),

    type_alias: $ => seq(
      'type',
      field('name', $.identifier),
      optional($.type_parameters),
      '=',
      field('type', $._type),
      ';',
    ),

    constraint_declaration: $ => seq(
      'constraint',
      field('name', $.identifier),
      optional($.type_parameters),
      '=',
      field('value', $._constraint_body),
      ';',
    ),

    _constraint_body: $ => choice(
      $.requires_block,
      $.constraint_union,
      $.constraint_intersection,
      $._type,
    ),

    requires_block: $ => seq(
      'requires',
      '{',
      commaSep($.required_function),
      '}',
    ),

    required_function: $ => seq(
      field('name', choice($.identifier, $.operator_name)),
      '(',
      commaSep($._type),
      ')',
      field('return_type', $._type),
    ),

    constraint_union: $ => seq(
      $._type, '|', sep1($._type, '|'),
    ),

    constraint_intersection: $ => seq(
      $._type, '&', sep1($._type, '&'),
    ),

    import_declaration: $ => seq(
      'import',
      $.import_path,
      optional(choice(
        seq(':', '*'),
        seq('.', '{', commaSep1($.import_specifier), '}'),
        seq('as', $.identifier),
      )),
      ';',
    ),

    import_path: $ => prec.right(sep1($.identifier, '.')),

    import_specifier: $ => seq(
      $.identifier,
      optional(seq('as', $.identifier)),
    ),

    // ===================== STATEMENTS =====================

    _statement: $ => choice(
      $.expression_statement,
      $.assignment,
      $.if_statement,
      $.while_statement,
      $.for_statement,
      $.return_statement,
      $.yield_statement,
      $.break_statement,
      $.continue_statement,
    ),

    expression_statement: $ => prec(-1, seq(
      $._expression,
      optional(';'),
    )),

    assignment: $ => seq(
      field('left', $._expression),
      '=',
      field('right', $._expression),
      ';',
    ),

    if_statement: $ => prec.right(seq(
      'if',
      '(',
      field('condition', $._expression),
      ')',
      field('consequence', $.block),
      optional(seq(
        'else',
        field('alternative', choice($.block, $.if_statement)),
      )),
    )),

    while_statement: $ => seq(
      'while',
      '(',
      field('condition', $._expression),
      ')',
      field('body', $.block),
    ),

    for_statement: $ => seq(
      'for',
      '(',
      field('variable', choice($.identifier, $.tuple_pattern)),
      ':',
      field('iterable', $._expression),
      ')',
      field('body', $.block),
    ),

    return_statement: $ => seq(
      'return',
      optional($._expression),
      ';',
    ),

    yield_statement: $ => seq(
      'yield',
      optional($._expression),
      ';',
    ),

    break_statement: $ => seq('break', ';'),

    continue_statement: $ => seq('continue', ';'),

    block: $ => seq(
      '{',
      repeat(choice(
        $._declaration,
        $._statement,
      )),
      '}',
    ),

    // ===================== EXPRESSIONS =====================

    _expression: $ => choice(
      $.primary_expression,
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.index_expression,
      $.field_expression,
      $.automap_expression,
      $.space_pipeline,
      $.ternary_expression,
      $.lambda_expression,
      $.match_expression,
      $.if_expression,
      $.tuple_expression,
      $.array_expression,
      $.struct_construction,
      $.struct_update,
      $.parenthesized_expression,
    ),

    primary_expression: $ => choice(
      $.identifier,
      $._literal,
      $.this_expression,
    ),

    _literal: $ => choice(
      $.integer_literal,
      $.float_literal,
      $.imaginary_literal,
      $.string_literal,
      $.triple_string_literal,
      $.guillemet_string_literal,
      $.symbol_literal,
      $.boolean_literal,
      $.nil_literal,
    ),

    binary_expression: $ => {
      const operators = [
        [PREC.ARROW, '<-', 'right'],
        [PREC.ARROW, '->', 'right'],
        [PREC.PIPELINE, '|>', 'left'],
        [PREC.OR, '||', 'left'],
        [PREC.AND, '&&', 'left'],
        [PREC.CONS, '::', 'right'],
        [PREC.EQUAL, '==', 'left'],
        [PREC.EQUAL, '!=', 'left'],
        [PREC.COMPARE, '<', 'left'],
        [PREC.COMPARE, '<=', 'left'],
        [PREC.COMPARE, '>', 'left'],
        [PREC.COMPARE, '>=', 'left'],
        [PREC.BIT_OR, '|', 'left'],
        [PREC.BIT_XOR, '^', 'left'],
        [PREC.BIT_AND, '&', 'left'],
        [PREC.SHIFT, '<<', 'left'],
        [PREC.SHIFT, '>>', 'left'],
        [PREC.SHIFT, '>>>', 'left'],
        [PREC.ADD, '+', 'left'],
        [PREC.ADD, '-', 'left'],
        [PREC.ADD, '$', 'left'],
        [PREC.MUL, '*', 'left'],
        [PREC.MUL, '/', 'left'],
        [PREC.MUL, '%', 'left'],
        [PREC.MUL, '//', 'left'],
        [PREC.ADD, '..', 'left'],
        [PREC.ADD, '..<', 'left'],
      ];

      return choice(...operators.map(([prec_val, op, assoc]) => {
        const rule = seq(
          field('left', $._expression),
          field('operator', op),
          field('right', $._expression),
        );
        return assoc === 'right' ? prec.right(prec_val, rule) : prec.left(prec_val, rule);
      }));
    },

    unary_expression: $ => prec(PREC.UNARY, seq(
      field('operator', choice('-', '!', '~', '&', '*')),
      field('operand', $._expression),
    )),

    call_expression: $ => prec(PREC.POSTFIX, seq(
      field('function', $._expression),
      '(',
      commaSep($._expression),
      ')',
    )),

    index_expression: $ => prec(PREC.POSTFIX, seq(
      field('object', $._expression),
      '[',
      field('index', $._expression),
      ']',
    )),

    field_expression: $ => prec(PREC.POSTFIX, seq(
      field('object', $._expression),
      '.',
      field('field', choice($.identifier, $.integer_literal)),
    )),

    automap_expression: $ => prec.left(PREC.POSTFIX, seq(
      field('operand', $._expression),
      field('operator', $.automap_operator),
    )),

    automap_operator: $ => token(choice(
      '@@@',
      '@@',
      seq('@', /[1-9]/),
      '@',
    )),

    space_pipeline: $ => prec.right(PREC.POSTFIX, seq(
      field('value', $._expression),
      field('function', $.identifier),
      optional(seq('(', commaSep($._expression), ')')),
    )),

    ternary_expression: $ => prec.right(PREC.TERNARY, seq(
      field('condition', $._expression),
      '?',
      field('consequence', $._expression),
      ':',
      field('alternative', $._expression),
    )),

    lambda_expression: $ => prec.right(1, seq(
      'fn',
      optional($.type_parameters),
      $.parameter_list,
      optional(field('return_type', $._type)),
      choice(
        seq('=', field('body', $._expression)),
        field('body', $.block),
      ),
    )),

    if_expression: $ => prec.right(1, seq(
      'if',
      '(',
      field('condition', $._expression),
      ')',
      field('consequence', $.block),
      'else',
      field('alternative', choice($.block, $.if_expression)),
    )),

    match_expression: $ => seq(
      'match',
      '(',
      field('value', $._expression),
      ')',
      '{',
      repeat($.match_arm),
      '}',
    ),

    match_arm: $ => seq(
      field('pattern', $.pattern),
      optional(seq('if', '(', field('guard', $._expression), ')')),
      ':',
      field('body', choice(
        seq($._expression, ';'),
        $.block,
      )),
    ),

    tuple_expression: $ => seq(
      '(',
      choice(
        seq($._expression, ','),
        seq($._expression, ',', commaSep1($._expression)),
      ),
      ')',
    ),

    parenthesized_expression: $ => seq(
      '(',
      $._expression,
      ')',
    ),

    array_expression: $ => seq(
      '[',
      commaSep($._expression),
      ']',
    ),

    struct_construction: $ => prec(PREC.POSTFIX, seq(
      field('type', $.identifier),
      '{',
      commaSep(choice(
        $.struct_field_value,
        $._expression,
      )),
      '}',
    )),

    struct_field_value: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expression),
    ),

    struct_update: $ => seq(
      '{',
      '...',
      field('base', $._expression),
      ',',
      commaSep1($.struct_field_value),
      '}',
    ),

    // ===================== PATTERNS =====================

    pattern: $ => choice(
      $.wildcard_pattern,
      $.literal_pattern,
      $.binding_pattern,
      $.tuple_pattern,
      $.array_pattern,
      $.cons_pattern,
      $.enum_pattern,
      $.struct_pattern,
    ),

    wildcard_pattern: $ => '_',

    literal_pattern: $ => choice(
      $.integer_literal,
      $.float_literal,
      $.string_literal,
      $.boolean_literal,
      $.nil_literal,
      $.symbol_literal,
    ),

    binding_pattern: $ => $.identifier,

    tuple_pattern: $ => seq(
      '(',
      commaSep1($.pattern),
      ')',
    ),

    array_pattern: $ => seq(
      '[',
      commaSep(choice(
        $.pattern,
        $.rest_pattern,
      )),
      ']',
    ),

    rest_pattern: $ => seq(
      '...',
      optional($.identifier),
    ),

    cons_pattern: $ => prec.right(PREC.CONS, seq(
      field('head', $.pattern),
      '::',
      field('tail', $.pattern),
    )),

    enum_pattern: $ => seq(
      field('type', $.identifier),
      '.',
      field('case', $.identifier),
      optional(seq('(', commaSep($.pattern), ')')),
    ),

    struct_pattern: $ => seq(
      field('type', $.identifier),
      choice(
        seq('{', commaSep(choice(
          seq($.identifier, ':', $.pattern),
          $.identifier,
        )), '}'),
        seq('(', commaSep($.pattern), ')'),
      ),
    ),

    // ===================== TYPES =====================

    _type: $ => choice(
      $.primitive_type,
      $.generic_type,
      $.array_type,
      $.map_type,
      $.tuple_type,
      $.paren_type,
      $.function_type,
      $.type_identifier,
    ),

    paren_type: $ => seq('(', $._type, ')'),

    primitive_type: $ => choice(
      'Int',
      'Float',
      'Bool',
      'String',
      'Symbol',
      'Void',
      'Fraction',
      'Complex',
    ),

    type_identifier: $ => $.identifier,

    generic_type: $ => prec(1, seq(
      field('name', $.identifier),
      '<',
      commaSep1($._type),
      '>',
    )),

    array_type: $ => seq(
      '[',
      $._type,
      ']',
    ),

    map_type: $ => seq(
      '[',
      field('key', $._type),
      ':',
      field('value', $._type),
      ']',
    ),

    tuple_type: $ => seq(
      '(',
      choice(
        seq($._type, ','),
        seq($._type, ',', commaSep1($._type)),
      ),
      ')',
    ),

    function_type: $ => seq(
      'fn',
      '(',
      commaSep($._type),
      ')',
      $._type,
    ),

    // ===================== LITERALS =====================

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    integer_literal: $ => token(choice(
      /[0-9][0-9_]*/,
      /0[xX][0-9a-fA-F][0-9a-fA-F_]*/,
    )),

    float_literal: $ => token(
      seq(
        /[0-9][0-9_]*/,
        '.',
        /[0-9][0-9_]*/,
        optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/)),
      ),
    ),

    imaginary_literal: $ => token(choice(
      seq(/[0-9][0-9_]*/, 'i'),
      seq(/[0-9][0-9_]*/, '.', /[0-9][0-9_]*/, 'i'),
    )),

    string_literal: $ => seq(
      '"',
      repeat(choice(
        $.string_content,
        $.escape_sequence,
      )),
      '"',
    ),

    string_content: $ => token.immediate(prec(1, /[^"\\]+/)),

    escape_sequence: $ => token.immediate(seq(
      '\\',
      choice(
        /[nrt\\0"]/,
        /u[0-9a-fA-F]{4}/,
        /U[0-9a-fA-F]{8}/,
      ),
    )),

    triple_string_literal: $ => seq(
      '"""',
      repeat(choice(
        $.triple_string_content,
        $.escape_sequence,
      )),
      '"""',
    ),

    triple_string_content: $ => token.immediate(prec(1, /([^"\\]|"[^"]|""[^"])+/)),

    guillemet_string_literal: $ => seq(
      '\u00ab',
      optional($.guillemet_content),
      '\u00bb',
    ),

    guillemet_content: $ => token.immediate(/[^\u00bb]+/),

    symbol_literal: $ => token(seq("'", /[a-zA-Z_][a-zA-Z0-9_]*/)),

    boolean_literal: $ => choice('true', 'false'),

    nil_literal: $ => 'nil',

    this_expression: $ => 'this',

    // ===================== COMMENTS =====================

    line_comment: $ => token(seq('--', /.*/)),
  },
});

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}

function sep1(rule, delimiter) {
  return seq(rule, repeat(seq(delimiter, rule)));
}
