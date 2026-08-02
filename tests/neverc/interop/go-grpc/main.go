package main

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/protobuf/types/known/wrapperspb"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: go-grpc-interop <host:port>")
		os.Exit(2)
	}
	connection, err := grpc.NewClient(os.Args[1],
		grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		panic(err)
	}
	defer connection.Close()
	contextValue, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	request := wrapperspb.Bytes([]byte("official-grpc-go"))
	response := new(wrapperspb.BytesValue)
	if err := connection.Invoke(contextValue, "/test.Echo/Unary", request, response); err != nil {
		panic(err)
	}
	if !bytes.Equal(response.Value, request.Value) {
		panic(fmt.Errorf("echo mismatch: %q", response.Value))
	}
	fmt.Println("official gRPC-Go interop passed")
}
