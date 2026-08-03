#include "json_flatten.h"

#include <assert.h>
#include <iostream>
#include <map>
#include <string>

int main() {
    const std::string input =
        "prefix {\"device\":{\"current_temperature\":21.5,\"eco\":true,"
        "\"label\":\"Nest \\u2603\"},\"arr\":[1,\"x\",null],\"empty\":{}} suffix";
    std::string json;
    assert(extract_json_object(input, &json));

    std::map<std::string, std::string> fields;
    std::string error;
    assert(flatten_json(json, &fields, &error));
    assert(fields["device.current_temperature"] == "21.5");
    assert(fields["device.eco"] == "true");
    assert(fields["device.label"] == "Nest \xe2\x98\x83");
    assert(fields["arr"] == "[1,\"x\",null]");
    assert(fields["arr[0]"] == "1");
    assert(fields["arr[1]"] == "x");
    assert(fields["arr[2]"] == "null");
    assert(fields["empty"] == "{}");

    fields.clear();
    assert(!flatten_json("{\"broken\":]", &fields, &error));
    assert(!error.empty());

    assert(json_escape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
    assert(json_key_is_sensitive("device.assigned_cred_secret"));
    assert(!json_key_is_sensitive("device.target_temperature"));
    assert(topic_component("a[2].b/c") == "a_2.b_c");

    std::cout << "picojson tests passed\n";
    return 0;
}
