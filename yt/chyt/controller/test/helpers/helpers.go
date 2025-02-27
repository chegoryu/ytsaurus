package helpers

import (
	"bytes"
	"io"
	"net/http"
	"os"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
	"go.ytsaurus.tech/library/go/core/log"
	"go.ytsaurus.tech/yt/chyt/controller/internal/agent"
	"go.ytsaurus.tech/yt/chyt/controller/internal/api"
	"go.ytsaurus.tech/yt/chyt/controller/internal/httpserver"
	"go.ytsaurus.tech/yt/chyt/controller/internal/monitoring"
	"go.ytsaurus.tech/yt/chyt/controller/internal/sleep"
	"go.ytsaurus.tech/yt/chyt/controller/internal/strawberry"
	"go.ytsaurus.tech/yt/go/guid"
	"go.ytsaurus.tech/yt/go/ypath"
	"go.ytsaurus.tech/yt/go/yson"
	"go.ytsaurus.tech/yt/go/yt"
	"go.ytsaurus.tech/yt/go/yttest"
)

type Env struct {
	*yttest.Env
	StrawberryRoot ypath.Path
}

func PrepareEnv(t *testing.T) *Env {
	env := yttest.New(t)

	strawberryRoot := env.TmpPath().Child("strawberry")

	_, err := env.YT.CreateNode(env.Ctx, strawberryRoot, yt.NodeMap, &yt.CreateNodeOptions{
		Recursive: true,
		Attributes: map[string]any{
			"controller_parameter": "default",
		},
	})
	require.NoError(t, err)

	_, err = env.YT.CreateObject(env.Ctx, yt.NodeAccessControlObjectNamespace, &yt.CreateObjectOptions{
		Attributes: map[string]any{
			"name": "sleep",
		},
		IgnoreExisting: true,
	})
	require.NoError(t, err)

	return &Env{env, strawberryRoot}
}

func GenerateAlias() string {
	return "chyt" + guid.New().String()
}

type RequestClient struct {
	Endpoint string
	Proxy    string
	User     string

	httpClient *http.Client
	t          *testing.T
	env        *Env
}

type Response struct {
	StatusCode int
	Body       yson.RawValue
}

func (c *RequestClient) MakeRequest(httpMethod string, command string, params api.RequestParams) Response {
	body, err := yson.Marshal(params)
	require.NoError(c.t, err)

	c.env.L.Debug("making http api request", log.String("command", command), log.Any("params", params))

	req, err := http.NewRequest(httpMethod, c.Endpoint+"/"+command, bytes.NewReader(body))
	require.NoError(c.t, err)

	req.Header.Set("Content-Type", "application/yson")
	req.Header.Set("X-YT-TestUser", c.User)

	rsp, err := c.httpClient.Do(req)
	require.NoError(c.t, err)

	body, err = io.ReadAll(rsp.Body)
	require.NoError(c.t, err)

	c.env.L.Debug("http api request finished",
		log.String("command", command),
		log.Any("params", params),
		log.Int("status_code", rsp.StatusCode),
		log.String("response_body", string(body)))

	return Response{
		StatusCode: rsp.StatusCode,
		Body:       yson.RawValue(body),
	}
}

func (c *RequestClient) MakePostRequest(command string, params api.RequestParams) Response {
	return c.MakeRequest(http.MethodPost, c.Proxy+"/"+command, params)
}

func (c *RequestClient) MakeGetRequest(command string, params api.RequestParams) Response {
	return c.MakeRequest(http.MethodGet, command, params)
}

func PrepareClient(t *testing.T, env *Env, proxy string, server *httpserver.HTTPServer) *RequestClient {
	go server.Run()
	t.Cleanup(server.Stop)
	server.WaitReady()

	client := &RequestClient{
		Endpoint:   "http://" + server.RealAddress(),
		Proxy:      proxy,
		User:       "root",
		httpClient: &http.Client{},
		t:          t,
		env:        env,
	}

	return client
}

func PrepareAPI(t *testing.T) (*Env, *RequestClient) {
	env := PrepareEnv(t)

	proxy := os.Getenv("YT_PROXY")

	c := api.HTTPAPIConfig{
		BaseAPIConfig: api.APIConfig{
			ControllerFactories: map[string]strawberry.ControllerFactory{
				"sleep": strawberry.ControllerFactory{
					Ctor: sleep.NewController,
				},
			},
			ControllerMappings: map[string]string{
				"*": "sleep",
			},
		},
		ClusterInfos: []strawberry.AgentInfo{
			{
				StrawberryRoot: env.StrawberryRoot,
				Stage:          "test_stage",
				Proxy:          proxy,
				Family:         "sleep",
			},
		},
		DisableAuth: true,
		Endpoint:    ":0",
	}
	apiServer := api.NewServer(c, env.L.Logger())
	return env, PrepareClient(t, env, proxy, apiServer)
}

func abortAllOperations(t *testing.T, env *Env) {
	// TODO(max42): introduce some unique annotation and abort only such operations. This would allow
	// running this testsuite on real cluster.
	ops, err := yt.ListAllOperations(env.Ctx, env.YT, &yt.ListOperationsOptions{State: &yt.StateRunning})
	require.NoError(t, err)
	for _, op := range ops {
		err := env.YT.AbortOperation(env.Ctx, op.ID, &yt.AbortOperationOptions{})
		require.NoError(t, err)
	}
}

func CreateAgent(env *Env, stage string) *agent.Agent {
	l := log.With(env.L.Logger(), log.String("agent_stage", stage))

	passPeriod := yson.Duration(time.Millisecond * 400)
	collectOpsPeriod := yson.Duration(time.Millisecond * 200)
	config := &agent.Config{
		Root:                    env.StrawberryRoot,
		PassPeriod:              &passPeriod,
		CollectOperationsPeriod: &collectOpsPeriod,
		Stage:                   stage,
	}

	agent := agent.NewAgent(
		"test",
		env.YT,
		l,
		sleep.NewController(l.WithName("strawberry"),
			env.YT,
			env.StrawberryRoot,
			"test",
			nil),
		config)

	return agent
}

func PrepareAgent(t *testing.T) (*Env, *agent.Agent) {
	env := PrepareEnv(t)

	abortAllOperations(t, env)

	agent := CreateAgent(env, "default")

	return env, agent
}

type DummyLeader struct{}

func (a DummyLeader) IsLeader() bool {
	return true
}

func PrepareMonitoring(t *testing.T) (*Env, *agent.Agent, *RequestClient) {
	env, agent := PrepareAgent(t)
	proxy := os.Getenv("YT_PROXY")

	c := monitoring.HTTPMonitoringConfig{
		Clusters:                     []string{proxy},
		Endpoint:                     ":2223",
		HealthStatusExpirationPeriod: time.Duration(time.Minute),
	}

	server := monitoring.NewServer(c, env.L.Logger(), DummyLeader{}, map[string]monitoring.Healther{
		proxy: agent,
	})
	return env, agent, PrepareClient(t, env, proxy, server)
}
