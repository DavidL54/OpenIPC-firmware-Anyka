package app

// Internal firmware uses the fixed media configuration in JSON. User settings,
// authentication, MSE and RTSP still use the existing camera gateway code.
import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"strings"
)

type flagConfig []string

func (c *flagConfig) String() string { return strings.Join(*c, ",") }
func (c *flagConfig) Set(v string) error { *c = append(*c, v); return nil }

var internalConfigs [][]byte

// Keep the upstream yaml field names without linking a second configuration
// parser. Assign fields directly so upstream json:"-" logging tags cannot
// accidentally suppress configuration fields.
func decodeInternal(data []byte, target any) error {
	value := reflect.ValueOf(target)
	if value.Kind() != reflect.Pointer || value.IsNil() {
		return errors.New("configuration target must be a pointer")
	}
	return decodeValue(data, value.Elem())
}

func decodeValue(data []byte, target reflect.Value) error {
	if target.Kind() != reflect.Struct {
		return json.Unmarshal(data, target.Addr().Interface())
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil { return err }
	for i := 0; i < target.NumField(); i++ {
		field := target.Type().Field(i)
		if field.PkgPath != "" { continue }
		key := strings.Split(field.Tag.Get("yaml"), ",")[0]
		if key == "-" { continue }
		if key == "" { key = strings.ToLower(field.Name) }
		if item, ok := fields[key]; ok {
			if err := decodeValue(item, target.Field(i)); err != nil {
				return fmt.Errorf("configuration field %s: %w", key, err)
			}
		}
	}
	return nil
}

func initConfig(paths flagConfig) {
	if len(paths) != 1 { panic("internal firmware requires one media JSON configuration") }
	data, err := os.ReadFile(paths[0])
	if err != nil { panic("cannot read internal media configuration") }
	if len(data) > 8192 || !json.Valid(data) { panic("invalid internal media configuration") }
	var value map[string]json.RawMessage
	if err := json.Unmarshal(data, &value); err != nil || value == nil {
		panic("internal media configuration must be an object")
	}
	ConfigPath, err = filepath.Abs(paths[0])
	if err != nil { panic("invalid internal configuration path") }
	internalConfigs = [][]byte{data}
	Info["config_path"] = ConfigPath
}

func LoadConfig(target any) {
	for _, data := range internalConfigs {
		if err := decodeInternal(data, target); err != nil { panic(err) }
	}
}

func PatchConfig(_ []string, _ any) error {
	return errors.New("internal media configuration is read-only")
}
