from tabnanny import verbose
import pandas as pd
import numpy as np
from sklearn.preprocessing import OneHotEncoder
from numpy.random import randn
import os 
from constants import max_packets_per_omega,max_payload_per_omega,training_stages
import tensorflow as tf
from tensorflow.keras import backend as K
from tensorflow.keras.layers import Dense, LSTM, Bidirectional, Concatenate, Input, Dropout, Reshape, Add
from tensorflow.keras.activations import relu
from tensorflow.keras.regularizers import l1
from tensorflow.keras.models import Model
from tensorflow.keras.utils import get_custom_objects
from tensorflow.keras.optimizers import Adam, SGD
from tensorflow.keras.callbacks import ModelCheckpoint, EarlyStopping, ReduceLROnPlateau
from tqdm import tqdm

tf.compat.v1.logging.set_verbosity(tf.compat.v1.logging.ERROR)

combined_main_features = np.load('Features/new_features_main.npy')
combined_silences = np.load('Features/new_silences.npy')
combined_labels = np.load('Features/new_labels.npy')

new_combined_main = list()
for a in combined_main_features:
    a[:,1] += a[:,2]
    a[:,4] += a[:,5]
    new_combined_main.append(np.concatenate([a[:,:2],a[:,3:5]],axis=1))

combined_main_features = np.array(new_combined_main)
r = combined_main_features.shape[1]
c = combined_main_features.shape[2]

ohe = OneHotEncoder(sparse=False)
labels_u = combined_labels.reshape(-1,1)
ohe_labels = ohe.fit_transform(labels_u)

np.random.seed(20)
def custom_split(features_main,silences,labels):
    idx_new = np.random.permutation(features_main.shape[0])
    features_main,silences,labels = features_main[idx_new], silences[idx_new], labels[idx_new]
    split = int(len(features_main)*0.7)
    X_train_feat = features_main[:split]
    X_test_feat = features_main[split:]

    X_train_silence = silences[:split]
    X_test_silence = silences[split:]

    Y_train = labels[:split]
    Y_test = labels[split:]

    return X_train_feat,X_test_feat,X_train_silence,X_test_silence,Y_train,Y_test

def generate_latent_points(n_samples,latent_dim=20):
    x_input = randn(latent_dim * n_samples)
    x_input = x_input.reshape(n_samples, latent_dim)
    return x_input

def get_real_samples(target_device):    
    idx_dev0 = np.where(combined_labels==target_device)[0]

    if len(idx_dev0)>0:
        new_main_features = combined_main_features[idx_dev0]
        new_silences = combined_silences[idx_dev0]
        new_labels = ohe.transform(combined_labels[idx_dev0].reshape(-1,1))
    else:
        new_main_features = np.array([])
        new_silences = np.array([])
        new_labels = np.array([])

    idx = np.random.permutation(new_main_features.shape[0])
    new_main_features,new_silences,new_labels = new_main_features[idx],new_silences[idx],new_labels[idx]

    return new_main_features,new_silences,new_labels

def generate_perturbed_samples(generators):
    devices = list(generators.keys())
    device = devices[0]
    new_main_features,final_silences,final_labels = get_real_samples(device)
    n_samples = new_main_features.shape[0]
    x_input = generate_latent_points(n_samples)
    perturbations = tf.round(generators[device].predict(x_input))
    final_main_features = np.add(new_main_features,perturbations)
    
    for i in range(1,len(devices)):
        device = devices[i]
        new_main_features,new_silences,new_labels = get_real_samples(device)
        n_samples = new_main_features.shape[0]
        if n_samples>0:
            x_input = generate_latent_points(n_samples)
            perturbations = tf.round(generators[device].predict(x_input))
            new_main_features = np.add(new_main_features,perturbations)

            final_main_features = np.concatenate((final_main_features,new_main_features))
            final_silences = np.concatenate((final_silences,new_silences))
            final_labels = np.concatenate((final_labels,new_labels))

    idx = np.random.permutation(final_main_features.shape[0])
    final_main_features,final_silences,final_labels = final_main_features[idx],final_silences[idx],final_labels[idx]
    
    return final_main_features, final_silences, final_labels

sgd = SGD()
adam = Adam()
early = EarlyStopping(monitor="val_loss",
                              mode="min",
                              patience=10)
reduceLROnPlat = ReduceLROnPlateau(monitor='val_loss', factor=0.3,
                                           patience=3,
                                           verbose=1, mode='min', min_delta=0.001, cooldown=3,
                                           min_lr=1e-9)  # epsilon here is a good starting value

gpus = tf.config.experimental.list_physical_devices('GPU')
if gpus:
    try:
        for gpu in gpus:
            tf.config.experimental.set_memory_growth(gpu, True)
    except RuntimeError as e:
        print(e)

def negative_categorical_crossentropy(yTrue,yPred):
    return 0.0 - K.categorical_crossentropy(yTrue,yPred)

get_custom_objects().update({'negative_categorical_crossentropy': negative_categorical_crossentropy})


def get_init_weights_matrix(input_shape,max_packets,max_bytes):
    div = input_shape[-1]//2
    tar_shape = (r,input_shape[-1])
    
    weights = np.ones(tar_shape)
    for col in range(weights.shape[1]):
        if col%div == 0:
            weights[:,col] = weights[:,col]*max_packets
        else:
            weights[:,col] =weights[:,col]*max_bytes
    return weights,tar_shape

class UpScale(tf.keras.layers.Layer):
    def __init__(self,input_shape,max_packets,max_bytes):
        super(UpScale, self).__init__()
        x,tar_shape = get_init_weights_matrix(input_shape,max_packets,max_bytes)
        value = x.flatten()
        init = tf.constant_initializer(value)
        self.multiplier = tf.Variable(
            init(shape=tar_shape, dtype=tf.float64),
            trainable=False,
        )
        
    def call(self, inputs):
        inputs = tf.cast(inputs, tf.float64)   
        temp = tf.math.multiply(inputs,self.multiplier)
        return temp

def define_discriminator():
    dtype = 'float64'
    dropout=0.2

    input_data_1 = Input(name='main_Features', shape = (r,c), dtype=dtype)
    input_data_2 = Input(name='silence', shape=(1), dtype=dtype)

    att_in = Bidirectional(LSTM(8,return_sequences=True,kernel_regularizer=l2()))(input_data_1)
    att_out = Bidirectional(LSTM(8,return_sequences=False))(att_in)

    concatted = Concatenate()([att_out,input_data_2])
    x = Dense(units=128, activation='tanh', name='fc')(concatted)
    x = Dropout(dropout, name='dropout_2')(x)

    y_pred = Dense(units=len(ohe_labels[0]), activation='softmax', name='softmax')(x) 

    K.clear_session()
    model = Model(inputs=[input_data_1,input_data_2], outputs=y_pred)
    model.compile(loss='categorical_crossentropy', optimizer=adam, metrics=['accuracy'])
    return model

def define_generator(device,max_packets,max_len):
    dtype = 'float64'
    dropout=0.2 

    input_data_1 = Input(name='noise', shape=(20,), dtype=dtype)
    pert = Dense(r*c,activation = 'sigmoid',use_bias=True, name='adversarial_noise')(input_data_1)
    pert = Reshape((r,c))(pert)
    perturbations = UpScale((r,c),max_packets,max_len)(pert)

    K.clear_session()
    model_name = 'generator_' + str(device)
    generator = Model(name = model_name ,inputs=input_data_1, outputs=perturbations)
    return generator

def define_gan(device, discriminator,max_packets,max_len):
    discriminator.trainable = False
    dtype = 'float64'
    dropout=0.2

    input_data_1 = Input(name='noise_main', shape=(20,), dtype=dtype)
    input_data_2 = Input(name='target_matrix', shape=(r,c), dtype=dtype)
    input_data_3 = Input(name='silence', shape=(1,), dtype=dtype)

    generator = define_generator(device,max_packets,max_len)
    
    perturbations = generator(input_data_1)

    new_inp = Add(name='merge')([perturbations,input_data_2])
    output = discriminator([new_inp,input_data_3])

    K.clear_session()
    model_name = 'final_model_' + str(device)
    model = Model(name = model_name, inputs=[input_data_1,input_data_2,input_data_3], outputs=output)
    model.compile(loss=negative_categorical_crossentropy,optimizer='nadam',metrics=['accuracy'])
    return model

def train(max_packets,max_len, n_epochs=training_stages):
    devices = [device for device in np.unique(combined_labels)]
    discriminator = tf.keras.models.load_model('Models/Discriminators/checkpoint_D0')   
    gans = dict()
    
    for device in devices:
        gans[device] = define_gan(device,discriminator,max_packets,max_len)     
    
    for i in tqdm(range(n_epochs)): 
        ep = 5
        er = 1
        if i == 0:
            ep = 1
            er = 2
        elif i == 1:
            ep = 3
        
        # print("~~~~~~~~~~~Epoch "+str(i+1)+"~~~~~~~~~~~~")
        for device in devices:
            main_features,silences,labels = get_real_samples(device)
            Noise = generate_latent_points(main_features.shape[0])
            # print("Epoch: "+str(i+1)+"    Device: "+str(device))
            gans[device].fit([Noise,main_features,silences],labels,epochs = ep,verbose=0)
        
        generators = dict()
        for device in devices:
            generators[device] = gans[device].layers[1]
            
        # print("~~~~~~~~~~~Re Training Discrimator~~~~~~~~~~~~")
        final_main_features, final_silences, final_labels = generate_perturbed_samples(generators)
        discriminator.fit([final_main_features,final_silences],final_labels,epochs = er,verbose=0)

        for device,generator in generators.items(): 
            save_path = 'Models/Generators/Epoch_'+str(i)+'/generator_'+str(device)
            generator.save(save_path)
            
        save_path = 'Models/Discriminators/Epoch_'+str(i)
        discriminator.save(save_path)


tf.keras.backend.clear_session()
train(max_packets_per_omega,max_payload_per_omega)