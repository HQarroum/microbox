module github.com/HQarroum/microbox

go 1.24.0

require github.com/urfave/cli/v3 v3.4.1

require golang.org/x/sys v0.36.0

require (
	github.com/apparentlymart/go-cidr v1.1.0
	github.com/coreos/go-iptables v0.8.0
	github.com/google/uuid v1.6.0
	github.com/goombaio/namegenerator v0.0.0-20181006234301-989e774b106e
	github.com/inhies/go-bytesize v0.0.0-20220417184213-4913239db9cf
	github.com/moby/sys/capability v0.4.0
	github.com/seccomp/libseccomp-golang v0.11.1
	github.com/vishvananda/netlink v1.3.1
	github.com/vishvananda/netns v0.0.5
	go.etcd.io/bbolt v1.4.3
)

require github.com/stretchr/testify v1.11.0 // indirect
