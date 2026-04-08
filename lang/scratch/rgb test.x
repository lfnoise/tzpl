

enum Color { red, green, blue }

fn cool(c Color) = if (c == Color.red) { false } else { true };
--fn cool(c Color) = match (c) { red : false; green : true; blue : true } ;
--fn cool(c Color) { match (c) { red : false; green : true; blue : true } }

Color.red cool println;
Color.green cool println;
Color.blue cool println;


