package app

// Camera logs already go to /tmp/go2rtc.log, which the authenticated Logs page
// reads. The inaccessible upstream admin log API needs no second RAM ring.
import (
	"io"
	"os"
	"github.com/rs/zerolog"
)

var Logger zerolog.Logger
type unusedAdminLog struct{}
func (unusedAdminLog) WriteTo(io.Writer) (int64, error) { return 0, nil }
func (unusedAdminLog) Reset() {}
var MemoryLog unusedAdminLog

func GetLogger(_ string) zerolog.Logger { return Logger }
func initLogger() {
	zerolog.TimeFieldFormat = zerolog.TimeFormatUnixMs
	Logger = zerolog.New(os.Stdout).With().Timestamp().Logger().Level(zerolog.InfoLevel)
}
