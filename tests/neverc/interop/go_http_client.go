package main

import (
	"bytes"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"
)

func request(client *http.Client, method, url string, body []byte) error {
	req, err := http.NewRequest(method, url, bytes.NewReader(body))
	if err != nil {
		return err
	}
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	responseBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	if resp.StatusCode != http.StatusOK || resp.Header.Get("X-NeverC-Interop") != "yes" {
		return fmt.Errorf("unexpected response: status=%d header=%q", resp.StatusCode,
			resp.Header.Get("X-NeverC-Interop"))
	}
	if method == http.MethodPost && !bytes.Equal(responseBody, body) {
		return fmt.Errorf("POST echo mismatch: %q", responseBody)
	}
	return nil
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: go_http_client <base-url>")
		os.Exit(2)
	}
	client := &http.Client{Timeout: 5 * time.Second}
	url := os.Args[1] + "/echo"
	if err := request(client, http.MethodGet, url, nil); err != nil {
		panic(err)
	}
	if err := request(client, http.MethodPost, url, []byte("go-net-http")); err != nil {
		panic(err)
	}
	fmt.Println("Go net/http interop passed")
}
