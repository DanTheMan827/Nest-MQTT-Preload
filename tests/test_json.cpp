#include "json_flatten.h"

#include <assert.h>
#include <iostream>
#include <map>
#include <string>

int main() {
    const std::string input = "prefix {\"device\":{\"current_temperature\":21.5,\"eco\":true},\"arr\":[1,\"x\"]} suffix";
    std::string json;
    assert(extract_json_object(input, &json));
    std::map<std::string, std::string> fields;
    std::string error;
    assert(flatten_json(json, &fields, &error));
    assert(fields["device.current_temperature"] == "21.5");
    assert(fields["device.eco"] == "true");
    assert(fields["arr[0]"] == "1");
    assert(fields["arr[1]"] == "x");
    assert(json_key_is_sensitive("device.assigned_cred_secret"));
    assert(!json_key_is_sensitive("device.target_temperature"));
    std::cout << "json tests passed\n";
    return 0;
}
