# Beetle TCP client

By default, the tcp server runs on port 3002. Passing ```--tcp-port``` sets
a different port. ```Beetleclient.py``` reads input provided by the user,
and prints responses from the server.

## Usage

The tcp client will first ask for connection params. See Beetle readme for
params. These are read line by line as 'key value' pairs.

To send requests:

* ```read handleNo```
* ```write handleNo FFFF...``` *(spaces are ignored in the hex portion)*
* ```FFFF...``` *(directly send hex)*

In the above examples, ```handleNo``` can be an int or an expression ```o+h```.

## Notes

For practical usage, ```gattcli.py``` in the ```pygatt``` module is more user
friendly and safe. This module is retained for experimentation.