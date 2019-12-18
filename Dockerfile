FROM 
#RUN git clone https://github.com/Team3487-RedPrideRobotics/RemoteDS.git
COPY . /RemoteDS
WORKDIR /RemoteDS
RUN cmake . && \
    make && \
    make install
CMD ["./RemoteDS"] 