#include <stdio.h>

void *json_parse_string(char *str);

int main() {
  // Valid JSON Test Cases
  void *res;

  // Simple object
  res = json_parse_string("{\"key\":\"value\"}");

  // Numbers (int, float, negative, exponent)
  json_parse_string(
      "{\"int\":123, \"float\":12.34, \"neg\":-56, \"exp\":1.23e4}");

  // Boolean and null
  json_parse_string(
      "{\"bool_true\":true, \"bool_false\":false, \"none\":null}");

  // Array of mixed values
  json_parse_string("[123, \"string\", true, null, {\"nested\":1}, [1,2,3]]");

  // Empty object and array
  json_parse_string("{}");
  json_parse_string("[]");

  // Escaped characters in strings
  json_parse_string(
      "{\"escaped\":\"Line\\nbreak\\tTab\\\\Backslash\\\"Quote\"}");

  // Unicode escape
  json_parse_string("{\"unicode\":\"\\u1234\"}");

  // Nested objects
  json_parse_string("{\"outer\":{\"inner\":{\"value\":42}}}");

  // Array of objects
  json_parse_string("[{\"a\":1}, {\"b\":2}, {\"c\":[3,4]}]");

  // Invalid JSON Test Cases
  printf("Invalid JSON\n");

  // Missing closing brace
  json_parse_string("{\"string\":\"string\",\"number\":10.07");

  // Trailing comma
  json_parse_string("{\"a\":1,}");

  // Single quotes (invalid in JSON)
  json_parse_string("{'key':'value'}");

  // Unescaped control character
  json_parse_string("{\"bad\":\"text\u0001\"}");

  // Undefined keyword
  json_parse_string("{\"value\":undefined}");

  // Extra colon
  json_parse_string("{\"a\"::1}");

  // Array with trailing comma
  json_parse_string("[1, 2, 3,]");

  // Incomplete value
  json_parse_string("{\"key\":tru}");

  // No quotes on keys (only allowed in JS, not JSON)
  json_parse_string("{key:\"value\"}");

  // Misplaced comma
  json_parse_string("{,\"key\":\"value\"}");

  // Multiple top-level values (not allowed in standard JSON)
  json_parse_string("{\"a\":1}{\"b\":2}");
}