FROM ubuntu:latest

COPY  . release

WORKDIR release

RUN apt-get update
RUN apt-get install -y python3
RUN apt-get install -y python3-pip
RUN pip3 install python-gnupg
RUN apt-get install hyperfine

CMD ["tail", "-f", "/dev/null"]