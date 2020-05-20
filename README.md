# Peer-to-Peer Bingo

A simple C implementation of a peer-to-peer bingo game.

## bingo.h
   Contains all functions related to playing bingo. This file can generate new balls, make a new board.

## client.c
   Creates the client that can communicate with the server to crate games. After starting the game all communication is p2p

## makefile
   Makefile for building the project

## msg.h
   Message format for sending files between client and server

## server.c
   Server for managing and maintaining connected users and games

## uthash.h
   Hash Table file for C
