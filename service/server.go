package main

import (
	"context"
	"encoding/json"
	"flag"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/signal"
	"runtime"
	"time"
)

type Config struct {
	ListenAddress     string        `json:"listen-address"`
	ParentAddress     string        `json:"parent-address"`
	KndConfigPath     string        `json:"shard-config"`
	RequestsMax       int           `json:"requests-max"`
	SlotAwaitDuration time.Duration `json:"slot-await-duration"`
}

var (
	cfg       *Config
	KndConfig string
)

// todo(n.rodionov): write a separate function for each {} excess block
func init() {
	var (
		configPath    string
		kndConfigPath string
		listenAddress string
		parentAddress string
		requestsMax   int
		duration      time.Duration
	)

	flag.StringVar(&configPath, "config-path", "/etc/knowdy/service.json", "path to http service config")
	flag.StringVar(&kndConfigPath, "knd-config-path", "/etc/knowdy/shard.gsl", "path to Knowdy config")
	flag.StringVar(&listenAddress, "listen-address", "127.0.0.1:8089", "Knowdy listen address")
	flag.StringVar(&parentAddress, "parent-address", "", "parent service address")
	flag.IntVar(&requestsMax, "requests-limit", 10, "maximum number of requests to process simultaneously")
	flag.DurationVar(&duration, "request-limit-duration", 1*time.Second, "free slot awaiting time")
	flag.Parse()

	{ // load config
		configData, err := ioutil.ReadFile(configPath)
		if err != nil {
			log.Fatalln("could not read service config, error:", err)
		}
		err = json.Unmarshal(configData, &cfg)
		if err != nil {
			log.Fatalln("could not unmarshal config file, error:", err)
		}
	}

	{ // redefine config with cmd-line parameters
		if kndConfigPath != "" {
			cfg.KndConfigPath = kndConfigPath
		}
		if listenAddress != "" {
			cfg.ListenAddress = listenAddress
		}
		if parentAddress != "" {
			cfg.ParentAddress = parentAddress
		}
	}

	{ // load shard config
		shardConfigBytes, err := ioutil.ReadFile(cfg.KndConfigPath)
		if err != nil {
			log.Fatalln("could not read shard config, error:", err)
		}
		KndConfig = string(shardConfigBytes)
	}

	if duration != 0 {
		cfg.SlotAwaitDuration = duration
	}
	if requestsMax != 0 {
		cfg.RequestsMax = requestsMax
	}
}

func main() {
	proc, err := New(KndConfig, cfg.ParentAddress, runtime.GOMAXPROCS(0))
	if err != nil {
		log.Fatalln("could not create kndShard, error:", err)
	}
	defer proc.Del()

	router := http.NewServeMux()
	router.Handle("/gsl", limiter(gslHandler(proc),
		cfg.RequestsMax, cfg.SlotAwaitDuration))
	// router.Handle("/metrics", metricsHandler)

	server := &http.Server{
		Handler:      logger(router),
		ReadTimeout:  5 * time.Second,
		WriteTimeout: 5 * time.Second,
		IdleTimeout:  15 * time.Second,
		Addr:         cfg.ListenAddress,
	}

	done := make(chan bool)
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt)

	go func() {
		<-quit
		log.Println("shutting down server...")

		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		server.SetKeepAlivesEnabled(false)
		if err := server.Shutdown(ctx); err != nil {
			log.Fatalln("could not gracefully shutdown the server:", server.Addr)
		}
		close(done)
	}()

	log.Println("Knowdy server is ready to handle requests at:", cfg.ListenAddress)

	err = server.ListenAndServe()
	if err != nil && err != http.ErrServerClosed {
		log.Fatalf("could not listen on %s, err: %v\n", server.Addr, err)
	}

	<-done
	log.Println("server stopped")
}

func logger(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		log.Println(r.Method, r.URL.Path, r.URL.Query(), r.RemoteAddr, r.UserAgent())
		h.ServeHTTP(w, r)
	})
}

func limiter(h http.Handler, requestsMax int, duration time.Duration) http.Handler {
	semaphore := make(chan struct{}, requestsMax)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case semaphore <- struct{}{}:
			defer func() { <-semaphore }()
			h.ServeHTTP(w, r)
		case <-time.After(duration):
			http.Error(w, "server is busy", http.StatusTooManyRequests)
			log.Println("no free slots")
			return
		}
	})
}

func gslHandler(proc *kndProc) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		defer r.Body.Close()
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "internal server error", http.StatusInternalServerError)
			return
		}
		result, _, err := proc.RunTask(string(body), len(body))
		if err != nil {
			http.Error(w, "internal server error: "+err.Error(),
				http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, result)
	})
}
